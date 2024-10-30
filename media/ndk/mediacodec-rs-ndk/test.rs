/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#[cfg(test)]
mod tests {
    use mediacodec::BinderProcess_startThreadPool;
    use mediacodec::{AsyncSessionMessage, MediaCodec, MediaError, MediaFormat};
    use mpeg4extractor::MPEG4Extractor;
    use std::time::{Duration, Instant};

    fn make_fourcc(s: &str) -> u32 {
        s.bytes().fold(0, |acc, b| acc << 8 | (b as u32))
    }

    fn make_codec_specific_data_avc(src: &[u8]) -> Vec<Vec<u8>> {
        let mut result = Vec::new();

        let num_sps = src[5] & 31;
        let mut offset = 6;

        for _ in 0..num_sps {
            let n = u16::from_be_bytes(src[offset..offset + 2].try_into().unwrap()) as usize;

            offset += 2;

            let mut sps = Vec::new();
            sps.extend_from_slice(&[0x00, 0x00, 0x00, 0x01]);
            sps.extend_from_slice(&src[offset..offset + n]);
            result.push(sps);

            offset += n;
        }

        let num_pps = src[offset];
        offset += 1;

        for _ in 0..num_pps {
            let n = u16::from_be_bytes(src[offset..offset + 2].try_into().unwrap()) as usize;

            offset += 2;

            let mut pps = Vec::new();
            pps.extend_from_slice(&[0x00, 0x00, 0x00, 0x01]);
            pps.extend_from_slice(&src[offset..offset + n]);
            result.push(pps);

            offset += n;
        }

        result
    }

    // At least the pixel decoder wants all the codec specific data packaged up in
    // a single buffer (for HEVC)...
    fn make_codec_specific_data_hevc(src: &[u8]) -> Vec<Vec<u8>> {
        assert!(src.len() >= 23);
        assert!(src[0] == 0 || src[0] == 1);

        let mut offset = 22;
        let num_arrays = src[offset];
        offset += 1;

        let mut nalu = Vec::new();

        for _ in 0..num_arrays {
            offset += 1;

            let num_nals = u16::from_be_bytes(src[offset..offset + 2].try_into().unwrap()) as usize;

            offset += 2;

            for _ in 0..num_nals {
                let nal_length =
                    u16::from_be_bytes(src[offset..offset + 2].try_into().unwrap()) as usize;

                offset += 2;

                nalu.extend_from_slice(&[0x00, 0x00, 0x00, 0x01]);
                nalu.extend_from_slice(&src[offset..offset + nal_length]);

                offset += nal_length;
            }
        }

        vec![nalu]
    }

    fn make_codec_specific_data_av1(src: &[u8]) -> Vec<Vec<u8>> {
        //         assert!(src.len() >= 4);
        //         assert!((src[0] & 0x80) != 0);  // marker == 1
        //         assert!((src[0] & 0x7f) != 0);  // version == 1
        //         assert!((src[3] >> 6) == 0);  // reserved == 0
        //
        //         vec![src[4..].to_vec()]

        vec![src.to_vec()]
    }

    fn make_codec_specific_data_mpeg4(src: &[u8]) -> Vec<Vec<u8>> {
        vec![src.to_vec()]
    }

    fn make_codec_specific_data(mime: &str, src: &[u8]) -> Vec<Vec<u8>> {
        match mime {
            "video/avc" => make_codec_specific_data_avc(src),
            "video/hevc" => make_codec_specific_data_hevc(src),
            "video/x-vnd.on2.vp8" => Vec::default(),
            "video/x-vnd.on2.vp9" => Vec::default(),
            "video/av01" => make_codec_specific_data_av1(src),
            "video/mp4v-es" => make_codec_specific_data_mpeg4(src),
            _ => panic!("CSD extraction from {mime} unimplemented."),
        }
    }

    // Assumes that nalu_len == 4 == sizeof(start_code)
    fn convert_sample(data: &mut [u8]) {
        let len = data.len();

        let mut i = 0;
        while i + 4 <= len {
            let nal_size = u32::from_be_bytes(data[i..(i + 4)].try_into().unwrap());

            data[i] = 0x00;
            data[i + 1] = 0x00;
            data[i + 2] = 0x00;
            data[i + 3] = 0x01;

            i += 4 + (nal_size as usize);
        }
    }

    #[test]
    fn test_instantiation() {
        assert!(MediaCodec::decoder_by_type("video/avc").is_ok());
    }

    #[test]
    fn test_create_empty_format() {
        assert!(MediaFormat::new().is_ok());
    }

    #[test]
    fn test_create_format_set_get() -> Result<(), MediaError> {
        let mut format = MediaFormat::new()?;
        format.set_i32("foo", 666)?;
        assert_eq!(format.get_i32("foo")?, 666);

        Ok(())
    }

    #[test]
    fn test_configure() -> Result<(), MediaError> {
        const MIME: &str = "video/avc";

        let mut codec = MediaCodec::decoder_by_type(MIME)?;

        let mut format = MediaFormat::new()?;
        format.set_string(MediaFormat::KEY_MIME, MIME)?;
        format.set_i32(MediaFormat::KEY_WIDTH, 640)?;
        format.set_i32(MediaFormat::KEY_HEIGHT, 360)?;

        codec.configure(&format)?;

        let mut session = codec.start()?;

        while let Ok(_msg) = session.read_message(Duration::from_millis(1000)) {}

        session.flush()?;
        session.stop()?;

        Ok(())
    }

    #[allow(dead_code)]
    fn hexdump(data: &[u8]) {
        let size = data.len();
        let mut offset = 0;
        while offset < size {
            let len = (size - offset).min(16);
            let mut line = std::format!("{offset:08x}: ");
            for i in 0..len {
                let byte = data[offset + i];
                line.push_str(std::format!("{byte:02x} ").as_str());

                if i == 7 {
                    line.push(' ');
                }
            }

            eprintln!("{line}");

            offset += len;
        }
    }

    #[test]
    fn test_decode_avc() -> Result<(), Box<dyn std::error::Error>> {
        const PATH: &str = "/data/big_buck_bunny_AVC.mp4";
        test_decode_file(PATH)
    }

    #[test]
    fn test_decode_avc2() -> Result<(), Box<dyn std::error::Error>> {
        const PATH: &str = "/data/BigBuckBunny_640x360_AVC.m4v";
        test_decode_file(PATH)
    }

    #[test]
    fn test_decode_hevc() -> Result<(), Box<dyn std::error::Error>> {
        // const PATH: &str = "/data/Big_Buck_Bunny_720_10s_1MB_HEVC.mp4";
        const PATH: &str = "/data/Big_Buck_Bunny_1080_10s_1MB_HEVC.mp4";
        test_decode_file(PATH)
    }

    #[test]
    fn test_decode_mpeg4() -> Result<(), Box<dyn std::error::Error>> {
        const PATH: &str = "/data/output_MP4V.mp4";
        test_decode_file(PATH)
    }

    #[test]
    fn test_decode_vp9() -> Result<(), Box<dyn std::error::Error>> {
        const PATH: &str = "/data/UshaikaRiverEmb_3840x2160_VP9.mp4";
        test_decode_file(PATH)
    }

    #[test]
    fn test_decode_av1() -> Result<(), Box<dyn std::error::Error>> {
        const PATH: &str = "/data/Sintel_1080_10s_1MB.mp4";
        test_decode_file(PATH)
    }

    fn test_decode_file(path: &str) -> Result<(), Box<dyn std::error::Error>> {
        BinderProcess_startThreadPool();

        let mut extractor = MPEG4Extractor::new(path)?;
        let tracks = extractor.get_tracks();

        let video_track =
            tracks.iter().find(|track| track.get_type().starts_with("video/")).unwrap();

        let mime = video_track.get_type();
        let (width, height) = video_track.get_dimensions();

        let (fourcc, data_needs_framing) = match mime.as_str() {
            "video/avc" => (Some(make_fourcc("avcC")), true),
            "video/hevc" => (Some(make_fourcc("hvcC")), true),
            "video/mp4v-es" => (Some(make_fourcc("esds")), false),
            "video/av01" => (Some(make_fourcc("av1C")), false),
            "video/x-vnd.on2.vp8" => (None, false),
            "video/x-vnd.on2.vp9" => (Some(make_fourcc("vpcC")), false),
            _ => panic!(),
        };

        let csd_nalus;

        if let Some(fourcc) = fourcc {
            let csd = video_track.get_codec_specific_data(fourcc)?;
            csd_nalus = make_codec_specific_data(&mime, &csd);
        } else {
            csd_nalus = Vec::default();
        }

        let mut codec = MediaCodec::decoder_by_type(&mime)?;

        let mut format = MediaFormat::new()?;
        format.set_string(MediaFormat::KEY_MIME, &mime)?;
        format.set_i32(MediaFormat::KEY_WIDTH, width.try_into().unwrap())?;
        format.set_i32(MediaFormat::KEY_HEIGHT, height.try_into().unwrap())?;

        codec.configure(&format)?;

        let session = codec.start()?;

        let mut num_csd_nalus_submitted = 0;
        let mut saw_input_eos = false;
        let mut saw_output_eos = false;
        let mut sample_index = 0;
        let start_time = Instant::now();
        let mut num_frames_decoded = 0;

        loop {
            let Ok(msg) = session.read_message(Duration::from_millis(1000)) else {
                continue;
            };

            match msg {
                AsyncSessionMessage::InputBufferAvailable(mut buffer) => {
                    #[cfg(verbose)]
                    println!("{buffer:?} now available.");

                    if num_csd_nalus_submitted < csd_nalus.len() {
                        let src = &csd_nalus[num_csd_nalus_submitted];

                        assert!(src.len() <= buffer.capacity());

                        let buffer_data = buffer.as_mut();
                        buffer_data[..src.len()].copy_from_slice(src);

                        buffer.queue(
                            0, /* offset */
                            src.len(),
                            0, /* time */
                            MediaCodec::BUFFER_FLAG_CODECCONFIG,
                        )?;

                        #[cfg(verbose)]
                        println!("Sent {} bytes of CSD data.", src.len());

                        num_csd_nalus_submitted += 1;
                    } else if !saw_input_eos {
                        if let Ok((offset, size)) = video_track.get_sample_at(sample_index) {
                            assert!(size <= buffer.capacity());

                            let buffer_data = buffer.as_mut();
                            extractor.read_at(offset, &mut buffer_data[..size])?;

                            if data_needs_framing {
                                convert_sample(&mut buffer_data[..size]);
                            }

                            buffer.queue(
                                0, /* offset */
                                size,
                                0, /* time */
                                0, /* flags */
                            )?;

                            sample_index += 1;

                            #[cfg(verbose)]
                            println!("Sent {size} bytes of data.");
                        } else {
                            saw_input_eos = true;

                            buffer.queue(
                                0, /* offset */
                                0, /* size */
                                0, /* time */
                                MediaCodec::BUFFER_FLAG_EOS,
                            )?;

                            #[cfg(verbose)]
                            println!("Signaled input EOS.");
                        }
                    }
                }
                AsyncSessionMessage::OutputBufferAvailable(output_buffer) => {
                    #[cfg(verbose)]
                    if !output_buffer.is_empty() {
                        println!("{output_buffer:?} now available.");
                    } else {
                        println!("empty {output_buffer:?}");
                    }

                    if !output_buffer.is_empty() {
                        num_frames_decoded += 1;
                    }

                    if (output_buffer.flags() & MediaCodec::BUFFER_FLAG_EOS) != 0 {
                        saw_output_eos = true;
                        #[cfg(verbose)]
                        println!("saw output EOS.");
                    }

                    // output_buffer is dropped and thereby reclaimed by the session.
                }
                #[allow(unused_variables)]
                AsyncSessionMessage::FormatChanged(format) => {
                    #[cfg(verbose)]
                    println!("output format changed to {format:?}");
                }
                _ => {
                    eprintln!("{msg:?}");
                    break;
                }
            }

            if saw_output_eos {
                break;
            }
        }
        session.stop()?;

        if !saw_output_eos {
            eprintln!("stream ended before seeing all output data.");
            return Err(Box::new(MediaError::UnknownError(-libc::EINVAL)));
        }

        let elapsed_time_ms = start_time.elapsed().as_millis();
        println!(
            "decoded {num_frames_decoded} frames in {elapsed_time_ms} ms. ({:.2} fps)",
            (1000.0 * (num_frames_decoded as f64)) / (elapsed_time_ms as f64)
        );

        Ok(())
    }
}
