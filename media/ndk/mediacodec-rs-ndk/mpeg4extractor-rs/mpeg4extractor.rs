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

#![allow(missing_docs)]
#![allow(dead_code)]
#![allow(clippy::undocumented_unsafe_blocks)]

use std::io::Error;

#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("libcxx_mpeg4extractor.hpp");

        type MPEG4Extractor;
        unsafe fn create_mpeg4_extractor(path: &str, err: &mut i32) -> UniquePtr<MPEG4Extractor>;

        unsafe fn mpeg4_read_structure(extractor: Pin<&mut MPEG4Extractor>);

        unsafe fn mpeg4_count_tracks(extractor: &MPEG4Extractor) -> usize;

        type MPEG4Track;
        unsafe fn mpeg4_get_track(
            extractor: Pin<&mut MPEG4Extractor>,
            index: usize,
        ) -> SharedPtr<MPEG4Track>;

        unsafe fn mpeg4_read_at(
            extractor: Pin<&mut MPEG4Extractor>,
            offset: u64,
            slice: &mut [u8],
        ) -> i32;

        unsafe fn mpeg4_track_get_type(track: &MPEG4Track) -> String;

        unsafe fn mpeg4_track_get_width(track: &MPEG4Track) -> u32;
        unsafe fn mpeg4_track_get_height(track: &MPEG4Track) -> u32;

        unsafe fn mpeg4_track_get_codec_specific_data(
            track: &MPEG4Track,
            fourcc: u32,
            result: &mut Vec<u8>,
        ) -> bool;

        unsafe fn mpeg4_track_get_sample_at(
            track: &MPEG4Track,
            index: usize,
            offset: &mut u64,
            size: &mut usize,
        ) -> bool;
    }
}

pub struct MPEG4Track(cxx::SharedPtr<ffi::MPEG4Track>);

impl MPEG4Track {
    pub fn get_type(&self) -> String {
        unsafe { ffi::mpeg4_track_get_type(self.0.as_ref().unwrap()) }
    }

    pub fn get_dimensions(&self) -> (u32, u32) {
        let width = unsafe { ffi::mpeg4_track_get_width(self.0.as_ref().unwrap()) };
        let height = unsafe { ffi::mpeg4_track_get_height(self.0.as_ref().unwrap()) };

        (width, height)
    }

    pub fn get_codec_specific_data(&self, fourcc: u32) -> Result<Vec<u8>, Error> {
        let mut result = Vec::new();
        let success = unsafe {
            ffi::mpeg4_track_get_codec_specific_data(self.0.as_ref().unwrap(), fourcc, &mut result)
        };

        if !success {
            Err(Error::from_raw_os_error(libc::ENOENT))
        } else {
            Ok(result)
        }
    }

    pub fn get_sample_at(&self, index: usize) -> Result<(u64, usize), Error> {
        let mut offset: u64 = 0;
        let mut size: usize = 0;

        let success = unsafe {
            ffi::mpeg4_track_get_sample_at(self.0.as_ref().unwrap(), index, &mut offset, &mut size)
        };

        if !success {
            Err(Error::from_raw_os_error(libc::ERANGE))
        } else {
            Ok((offset, size))
        }
    }
}

impl std::fmt::Debug for MPEG4Track {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "MPEG4Track(type={})", self.get_type())
    }
}

pub struct MPEG4Extractor(cxx::UniquePtr<ffi::MPEG4Extractor>);

impl MPEG4Extractor {
    pub fn new(path: &str) -> Result<Self, Error> {
        let mut err: i32 = 0;
        let mut extractor = unsafe { ffi::create_mpeg4_extractor(path, &mut err) };

        if extractor.is_null() {
            Err(Error::from_raw_os_error(-err))
        } else {
            unsafe { ffi::mpeg4_read_structure(extractor.pin_mut()) };
            Ok(Self(extractor))
        }
    }

    pub fn get_tracks(&mut self) -> Vec<MPEG4Track> {
        let n = unsafe { ffi::mpeg4_count_tracks(self.0.as_ref().unwrap()) };

        (0..n)
            .map(|index| MPEG4Track(unsafe { ffi::mpeg4_get_track(self.0.pin_mut(), index) }))
            .collect()
    }

    pub fn read_at(&mut self, offset: u64, slice: &mut [u8]) -> Result<(), Error> {
        let err = unsafe { ffi::mpeg4_read_at(self.0.pin_mut(), offset, slice) };

        if err == 0 {
            Ok(())
        } else {
            Err(Error::from_raw_os_error(-err))
        }
    }
}
