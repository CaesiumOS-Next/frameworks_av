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
    use mediacodec::{MediaCodec, MediaError, MediaFormat};
    use std::time::Duration;

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
}
