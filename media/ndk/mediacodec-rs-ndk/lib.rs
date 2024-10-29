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

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::ptr::NonNull;

#[allow(non_camel_case_types)]
type c_size_t = usize; // use core::ffi::c_size_t;

#[allow(non_camel_case_types)]
type c_off_t = usize;

use std::sync::mpsc::{channel, Receiver, Sender};
use std::time::Duration;

type AMediaCodec = c_void;
type AMediaFormat = c_void;

#[allow(non_snake_case)]
#[repr(C)]
#[derive(Clone, Debug)]
pub struct AMediaCodecBufferInfo {
    pub offset: i32,
    pub size: i32,
    pub presentationTimeUs: i64,
    pub flags: u32,
}

#[allow(non_snake_case)]
#[repr(C)]
struct AMediaCodecOnAsyncNotifyCallback {
    onAsyncInputAvailable: extern "C" fn(*mut AMediaCodec, *mut c_void, i32),

    onAsyncOutputAvailable:
        extern "C" fn(*mut AMediaCodec, *mut c_void, i32, *const AMediaCodecBufferInfo),

    onAsyncFormatChanged: extern "C" fn(*mut AMediaCodec, *mut c_void, *mut AMediaFormat),
    onAsyncError: extern "C" fn(*mut AMediaCodec, *mut c_void, i32, i32, *const c_char),
}

extern "C" {
    fn AMediaCodec_createDecoderByType(mime_type: *const c_char) -> *mut AMediaCodec;

    fn AMediaCodec_delete(codec: *mut AMediaCodec);

    fn AMediaCodec_configure(
        codec: *mut AMediaCodec,
        format: *const c_void,
        surface: *const c_void,
        crypto: *const c_void,
        flags: u32,
    ) -> i32;

    fn AMediaCodec_start(codec: *mut AMediaCodec) -> i32;
    fn AMediaCodec_stop(codec: *mut AMediaCodec) -> i32;
    fn AMediaCodec_flush(codec: *mut AMediaCodec) -> i32;

    fn AMediaCodec_getOutputFormat(codec: *mut AMediaCodec) -> *mut AMediaFormat;

    fn AMediaCodec_setAsyncNotifyCallback(
        codec: *mut AMediaCodec,
        callback: AMediaCodecOnAsyncNotifyCallback,
        userdata: *const c_void,
    ) -> i32;

    fn AMediaCodec_getInputBuffer(
        codec: *mut AMediaCodec,
        index: c_size_t,
        out_side: *mut c_size_t,
    ) -> *mut u8;

    fn AMediaCodec_getOutputBuffer(
        codec: *mut AMediaCodec,
        index: c_size_t,
        out_side: *mut c_size_t,
    ) -> *const u8;

    fn AMediaCodec_queueInputBuffer(
        codec: *mut AMediaCodec,
        index: c_size_t,
        offset: c_off_t,
        size: c_size_t,
        time: u64,
        flags: u32,
    ) -> i32;

    fn AMediaCodec_releaseOutputBuffer(
        codec: *mut AMediaCodec,
        index: c_size_t,
        render: c_int,
    ) -> i32;

    fn AMediaFormat_new() -> *mut AMediaFormat;
    fn AMediaFormat_delete(format: *mut AMediaFormat);
    fn AMediaFormat_copy(to: *mut AMediaFormat, from: *mut AMediaFormat) -> i32;

    fn AMediaFormat_setString(format: *mut AMediaFormat, name: *const c_char, value: *const c_char);

    fn AMediaFormat_setInt32(format: *mut AMediaFormat, name: *const c_char, value: i32);

    fn AMediaFormat_getInt32(
        format: *mut AMediaFormat,
        name: *const c_char,
        value: *mut i32,
    ) -> c_int;

    fn AMediaFormat_getRect(
        format: *mut AMediaFormat,
        name: *const c_char,
        left: *mut i32,
        top: *mut i32,
        right: *mut i32,
        bottom: *mut i32,
    ) -> c_int;

    fn AMediaFormat_toString(format: *mut AMediaFormat) -> *const c_char;

    fn ABinderProcess_startThreadPool();
}

#[allow(non_snake_case)]
pub fn BinderProcess_startThreadPool() {
    // SAFETY: always safe to call
    unsafe { ABinderProcess_startThreadPool() };
}

#[derive(Debug)]
pub enum AsyncMessage {
    InputBufferAvailable(i32),
    OutputBufferAvailable(i32, AMediaCodecBufferInfo),
    Error(i32, i32, CString),
    FormatChanged(MediaFormat),
}

#[no_mangle]
#[allow(unused_variables)]
extern "C" fn onAsyncErrorCb(
    _codec: *mut AMediaCodec,
    userdata: *mut c_void,
    error: i32,
    action_code: i32,
    detail: *const c_char,
) {
    let msg = AsyncMessage::Error(
        error,
        action_code,
        // SAFETY: safe because detail is by contract a valid, non-null, pointer to
        // a valid, nul-terminated, C string.
        CString::from(unsafe { CStr::from_ptr(detail) }),
    );

    let sender = userdata as *const Sender<AsyncMessage>;
    // SAFETY: userdata was provided by us and known to be a valid, non-null,
    // pointer to a Sender object.
    unsafe { (*sender).send(msg).unwrap() };
}

#[no_mangle]
#[allow(unused_variables)]
extern "C" fn onAsyncFormatChangedCb(
    _codec: *mut AMediaCodec,
    userdata: *mut c_void,
    format: *mut AMediaFormat,
) {
    // SAFETY: safe because API contract guarantees that "format" is a valid,
    // non-null pointer to a MediaFormat.
    let Ok(format) = (unsafe { MediaFormat::clone_from_native(format) }) else {
        eprintln!("Unable to clone MediaFormat, no message sent!");
        return;
    };

    let msg = AsyncMessage::FormatChanged(format);

    let sender = userdata as *const Sender<AsyncMessage>;
    // SAFETY: userdata was provided by us and known to be a valid, non-null,
    // pointer to a Sender object.
    unsafe { (*sender).send(msg).unwrap() };
}

#[no_mangle]
#[allow(unused_variables)]
extern "C" fn onAsyncInputAvailableCb(_codec: *mut AMediaCodec, userdata: *mut c_void, index: i32) {
    let sender = userdata as *const Sender<AsyncMessage>;
    // SAFETY: userdata was provided by us and known to be a valid, non-null,
    // pointer to a Sender object.
    unsafe { (*sender).send(AsyncMessage::InputBufferAvailable(index)).unwrap() };
}

#[no_mangle]
#[allow(unused_variables)]
extern "C" fn onAsyncOutputAvailableCb(
    _codec: *mut AMediaCodec,
    userdata: *mut c_void,
    index: i32,
    info: *const AMediaCodecBufferInfo,
) {
    let sender = userdata as *const Sender<AsyncMessage>;
    // SAFETY: info is by API contract guaranteed to be a valid, non-null, pointer
    // to an AMediaCodecBufferInfo structure.
    let info = unsafe { (*info).clone() };
    // SAFETY: userdata was provided by us and known to be a valid, non-null,
    // pointer to a Sender object.
    unsafe { (*sender).send(AsyncMessage::OutputBufferAvailable(index, info)).unwrap() };
}

////////////////////////////////////////////////////////////////////////////////

#[derive(Debug, Copy, Clone)]
pub enum MediaError {
    NoMemory,
    NotFound,
    InsufficientResources,
    Reclaimed,
    UnknownError(i32),
    Malformed,
    Unsupported,
    InvalidObject,
    InvalidParameter,
    InvalidOperation,
    EndOfStream,
    IoError,
    WouldBlock,
}

impl std::fmt::Display for MediaError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}

impl std::error::Error for MediaError {}

impl From<i32> for MediaError {
    fn from(err: i32) -> Self {
        match err {
            1100 => Self::InsufficientResources,
            1101 => Self::Reclaimed,
            n if n == -libc::ENOMEM => Self::NoMemory,
            n if n == -libc::ENOENT => Self::NotFound,
            0 => panic!(),
            _ => Self::UnknownError(err),
        }
    }
}

impl From<MediaError> for i32 {
    fn from(err: MediaError) -> i32 {
        match err {
            MediaError::NoMemory => libc::ENOMEM,
            MediaError::NotFound => libc::ENOENT,
            _ => libc::EINVAL,
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

pub struct MediaRect {
    pub left: i32,
    pub top: i32,
    pub right: i32,
    pub bottom: i32,
}

impl MediaRect {
    pub fn new(left: i32, top: i32, right: i32, bottom: i32) -> Self {
        Self { left, top, right, bottom }
    }
}

pub struct MediaFormat {
    format: NonNull<c_void>,
}

// SAFETY: Android's AMediaFormat object is portable across thread boundaries,
// and so is our wrapper.
unsafe impl std::marker::Send for MediaFormat {}

// SAFETY: Android's AMediaFormat object is portable across thread boundaries,
// and so is our wrapper.
unsafe impl std::marker::Sync for MediaFormat {}

impl MediaFormat {
    pub const KEY_MIME: &'static str = "mime";
    pub const KEY_WIDTH: &'static str = "width";
    pub const KEY_HEIGHT: &'static str = "height";
    pub const KEY_COLOR_FORMAT: &'static str = "color-format";
    pub const KEY_STRIDE: &'static str = "stride";
    pub const KEY_SLICE_HEIGHT: &'static str = "slice-height";
    pub const KEY_DISPLAY_CROP: &'static str = "crop";

    #[allow(non_upper_case_globals)]
    pub const COLOR_FormatYUV420SemiPlanar: i32 = 21;
    #[allow(non_upper_case_globals)]
    pub const COLOR_FormatYUV420Planar: i32 = 19;

    pub fn new() -> Result<Self, MediaError> {
        let format =
            // SAFETY: safe to call
            NonNull::new(unsafe { AMediaFormat_new() })
            .ok_or(MediaError::NoMemory)?;

        Ok(Self { format })
    }

    /// # Safety
    /// Takes ownership of the provided NonNull<AMediaFormat> object.
    pub unsafe fn from_native(format: NonNull<c_void>) -> Self {
        Self { format }
    }

    unsafe fn clone_from_native(from: *mut AMediaFormat) -> Result<Self, MediaError> {
        let format =
            // SAFETY: safe to call
            NonNull::new(unsafe { AMediaFormat_new() })
            .ok_or(MediaError::NoMemory)?;

        // SAFETY: "format" and "from" are known to be a valid, non-null pointer.
        let err = unsafe { AMediaFormat_copy(format.as_ptr(), from) };

        if err != 0 {
            // SAFETY: "format" is known to be a valid, non-null pointer.
            unsafe { AMediaFormat_delete(format.as_ptr()) };
            return Err(MediaError::from(err));
        }

        Ok(Self { format })
    }

    pub fn set_string(&mut self, name: &str, value: &str) -> Result<(), MediaError> {
        let name = CString::new(name).map_err(|_| -libc::EINVAL)?;
        let value = CString::new(value).map_err(|_| -libc::EINVAL)?;

        // SAFETY: "format" is known to be a valid, non-null pointer, name and value
        // have just been constructed and are known to be valid.
        unsafe {
            AMediaFormat_setString(
                self.format.as_ptr(),
                name.as_c_str().as_ptr(),
                value.as_c_str().as_ptr(),
            )
        };

        Ok(())
    }

    pub fn set_i32(&mut self, name: &str, value: i32) -> Result<(), MediaError> {
        let name = CString::new(name).map_err(|_| -libc::EINVAL)?;

        // SAFETY: "format" is known to be a valid, non-null pointer, "name" has just
        // been constructed and is known to be valid.
        unsafe { AMediaFormat_setInt32(self.format.as_ptr(), name.as_c_str().as_ptr(), value) };

        Ok(())
    }

    pub fn get_i32(&self, name: &str) -> Result<i32, MediaError> {
        let name = CString::new(name).map_err(|_| -libc::EINVAL)?;

        let mut value: i32 = 0;

        // SAFETY: "format" is known to be a valid, non-null pointer, "name" has just
        // been constructed and is known to be valid.
        let success = unsafe {
            AMediaFormat_getInt32(self.format.as_ptr(), name.as_c_str().as_ptr(), &mut value)
        };

        if success == 0 {
            return Err(MediaError::NotFound);
        }

        Ok(value)
    }

    pub fn get_rect(&self, name: &str) -> Result<MediaRect, MediaError> {
        let name = CString::new(name).map_err(|_| -libc::EINVAL)?;

        let mut left: i32 = 0;
        let mut top: i32 = 0;
        let mut right: i32 = 0;
        let mut bottom: i32 = 0;

        // SAFETY: "format" is known to be a valid, non-null pointer, "name" has just
        // been constructed and is known to be valid.
        let success = unsafe {
            AMediaFormat_getRect(
                self.format.as_ptr(),
                name.as_c_str().as_ptr(),
                &mut left,
                &mut top,
                &mut right,
                &mut bottom,
            )
        };

        if success == 0 {
            return Err(MediaError::NotFound);
        }

        Ok(MediaRect { left, top, right, bottom })
    }

    pub fn try_clone(&self) -> Result<Self, MediaError> {
        // SAFETY: "format" is known to be a valid, non-null pointer.
        unsafe { Self::clone_from_native(self.format.as_ptr()) }
    }
}

impl std::fmt::Debug for MediaFormat {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        // SAFETY: "format" is known to be a valid, non-null pointer,
        // AMediaFormat_toString returns a valid utf8-encoded, nul-terminated,
        // C string by API contract.
        let c_str = unsafe {
            let bytes = AMediaFormat_toString(self.format.as_ptr());
            CStr::from_ptr(bytes)
        };
        write!(f, "{c_str:?}")
    }
}

impl Drop for MediaFormat {
    fn drop(&mut self) {
        // SAFETY: "format" is known to be a valid, non-null pointer.
        unsafe { AMediaFormat_delete(self.format.as_ptr()) };
    }
}

pub struct MediaCodec {
    codec: NonNull<c_void>,

    // "sender_box" holds the reference that's passed to
    // AMediaCodec_setAsyncNotifyCallback as a raw pointer below.
    #[allow(dead_code)]
    sender_box: Box<Sender<AsyncMessage>>,

    receiver: Receiver<AsyncMessage>,
}

// SAFETY: Android's AMediaCodec is known to be portable across thread boundaries
// and so is this wrapper.
unsafe impl std::marker::Send for MediaCodec {}

// SAFETY: Android's AMediaCodec is known to be portable across thread boundaries
// and so is this wrapper.
unsafe impl std::marker::Sync for MediaCodec {}

impl MediaCodec {
    pub const BUFFER_FLAG_CODECCONFIG: u32 = 2;
    pub const BUFFER_FLAG_EOS: u32 = 4;

    pub fn decoder_by_type(mime: &str) -> Result<Self, MediaError> {
        let mime = CString::new(mime).map_err(|_| -libc::EINVAL)?;

        // SAFETY: "mime" has just been constructed and verified as a valid,
        // non-null, pointer to a C string.
        let codec =
            NonNull::new(unsafe { AMediaCodec_createDecoderByType(mime.as_c_str().as_ptr()) })
                .ok_or(MediaError::NotFound)?;

        let (sender, receiver) = channel();

        let callbacks = AMediaCodecOnAsyncNotifyCallback {
            onAsyncError: onAsyncErrorCb,
            onAsyncFormatChanged: onAsyncFormatChangedCb,
            onAsyncInputAvailable: onAsyncInputAvailableCb,
            onAsyncOutputAvailable: onAsyncOutputAvailableCb,
        };

        let sender_box = Box::new(sender);
        let userdata = sender_box.as_ref() as *const Sender<AsyncMessage> as *const c_void;

        // SAFETY: "codec" is known to be a valid, non-null, pointer, "callbacks"
        // have just been constructed. AMediaCodec_setAsyncNotifyCallback will
        // not reference the pointer to "callbacks" after the call returns.
        let result =
            unsafe { AMediaCodec_setAsyncNotifyCallback(codec.as_ptr(), callbacks, userdata) };

        if result != 0 {
            // SAFETY: "codec" is known to be a valid, non-null pointer.
            unsafe { AMediaCodec_delete(codec.as_ptr()) };
            return Err(MediaError::from(result));
        }

        Ok(Self { codec, sender_box, receiver })
    }

    pub fn configure(&mut self, format: &MediaFormat) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer, format
        // is known to hold a valid, non-null pointer to the underlying
        // AMediaFormat object.
        let err = unsafe {
            AMediaCodec_configure(
                self.codec.as_ptr(),
                format.format.as_ptr(), // format
                std::ptr::null(),       // surface
                std::ptr::null(),       // crypto
                0,                      /* flags */
            )
        };

        if err != 0 {
            return Err(MediaError::from(err));
        }

        Ok(())
    }

    pub fn start(&mut self) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let result = unsafe { AMediaCodec_start(self.codec.as_ptr()) };

        if result != 0 {
            return Err(MediaError::from(result));
        }

        Ok(())
    }

    pub fn stop(&mut self) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let result = unsafe { AMediaCodec_stop(self.codec.as_ptr()) };

        if result != 0 {
            return Err(MediaError::from(result));
        }

        Ok(())
    }

    pub fn flush(&mut self) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let result = unsafe { AMediaCodec_flush(self.codec.as_ptr()) };

        if result != 0 {
            return Err(MediaError::from(result));
        }

        Ok(())
    }

    pub fn get_output_format(&self) -> Result<MediaFormat, MediaError> {
        let format =
            // SAFETY: "codec" is known to be a valid, non-null pointer.
            NonNull::new(unsafe { AMediaCodec_getOutputFormat(self.codec.as_ptr()) })
            .ok_or(MediaError::NoMemory)?;

        Ok(MediaFormat { format })
    }

    pub fn get_input_buffer(&self, index: usize) -> Result<&mut [u8], MediaError> {
        let mut size: usize = 0;
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let ptr = unsafe { AMediaCodec_getInputBuffer(self.codec.as_ptr(), index, &mut size) };

        if ptr.is_null() {
            return Err(MediaError::NoMemory);
        }

        // SAFETY: API contract states that (ptr, size) references a contiguous
        // range of accessible memory.
        Ok(unsafe { std::slice::from_raw_parts_mut::<u8>(ptr, size) })
    }

    pub fn get_output_buffer(&self, index: usize) -> Result<&[u8], MediaError> {
        let mut size: usize = 0;
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let ptr = unsafe { AMediaCodec_getOutputBuffer(self.codec.as_ptr(), index, &mut size) };

        if ptr.is_null() {
            return Err(MediaError::NoMemory);
        }

        // SAFETY: API contract states that (ptr, size) references a contiguous
        // range of accessible memory.
        Ok(unsafe { std::slice::from_raw_parts::<u8>(ptr, size) })
    }

    pub fn queue_input_buffer(
        &self,
        index: usize,
        offset: usize,
        size: usize,
        time: u64,
        flags: u32,
    ) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let err = unsafe {
            AMediaCodec_queueInputBuffer(self.codec.as_ptr(), index, offset, size, time, flags)
        };

        if err != 0 {
            return Err(MediaError::from(err));
        }

        Ok(())
    }

    pub fn release_output_buffer(&self, index: usize, output: bool) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let err =
            unsafe { AMediaCodec_releaseOutputBuffer(self.codec.as_ptr(), index, output as c_int) };

        if err != 0 {
            return Err(MediaError::from(err));
        }

        Ok(())
    }

    pub fn read_message(&self, timeout: Duration) -> Option<AsyncMessage> {
        if let Ok(msg) = self.receiver.recv_timeout(timeout) {
            return Some(msg);
        }

        None
    }
}

impl Drop for MediaCodec {
    fn drop(&mut self) {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        unsafe { AMediaCodec_delete(self.codec.as_ptr()) };
    }
}
