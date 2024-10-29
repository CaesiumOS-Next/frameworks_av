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

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::ptr::NonNull;

#[allow(non_camel_case_types)]
type c_size_t = usize; // use core::ffi::c_size_t;

#[allow(non_camel_case_types)]
type c_off_t = usize;

use std::pin::Pin;
use std::sync::mpsc::{channel, Receiver, Sender};
use std::sync::Mutex;
use std::time::Duration;

// https://doc.rust-lang.org/nomicon/ffi.html#representing-opaque-structs

#[repr(C)]
struct AMediaCodec {
    _data: [u8; 0],
    _marker: core::marker::PhantomData<(*mut u8, core::marker::PhantomPinned)>,
}

#[repr(C)]
pub struct AMediaFormat {
    _data: [u8; 0],
    _marker: core::marker::PhantomData<(*mut u8, core::marker::PhantomPinned)>,
}

#[allow(non_camel_case_types)]
type media_status_t = c_int;

#[allow(non_snake_case)]
#[repr(C)]
#[derive(Clone, Debug)]
pub(crate) struct AMediaCodecBufferInfo {
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
    onAsyncError: extern "C" fn(*mut AMediaCodec, *mut c_void, media_status_t, i32, *const c_char),
}

extern "C" {
    fn AMediaCodec_createDecoderByType(mime_type: *const c_char) -> *mut AMediaCodec;

    fn AMediaCodec_delete(codec: *mut AMediaCodec);

    fn AMediaCodec_configure(
        codec: *mut AMediaCodec,
        format: *const AMediaFormat,
        surface: *const c_void,
        crypto: *const c_void,
        flags: u32,
    ) -> media_status_t;

    fn AMediaCodec_start(codec: *mut AMediaCodec) -> media_status_t;
    fn AMediaCodec_stop(codec: *mut AMediaCodec) -> media_status_t;
    fn AMediaCodec_flush(codec: *mut AMediaCodec) -> media_status_t;

    fn AMediaCodec_getOutputFormat(codec: *mut AMediaCodec) -> *mut AMediaFormat;

    fn AMediaCodec_setAsyncNotifyCallback(
        codec: *mut AMediaCodec,
        callback: AMediaCodecOnAsyncNotifyCallback,
        userdata: *const c_void,
    ) -> media_status_t;

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
    ) -> media_status_t;

    fn AMediaCodec_releaseOutputBuffer(
        codec: *mut AMediaCodec,
        index: c_size_t,
        render: c_int,
    ) -> media_status_t;

    fn AMediaFormat_new() -> *mut AMediaFormat;
    fn AMediaFormat_delete(format: *mut AMediaFormat);
    fn AMediaFormat_copy(to: *mut AMediaFormat, from: *mut AMediaFormat) -> media_status_t;

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
pub(crate) enum AsyncMessage {
    InputBufferAvailable(i32),
    OutputBufferAvailable(i32, AMediaCodecBufferInfo),
    Error(media_status_t, i32, CString),
    FormatChanged(MediaFormat),
}

#[derive(Debug)]
pub enum AsyncSessionMessage<'a> {
    InputBufferAvailable(InputBuffer<'a>),
    OutputBufferAvailable(OutputBuffer<'a>),
    Error(media_status_t, i32, CString),
    FormatChanged(MediaFormat),
}

#[no_mangle]
#[allow(unused_variables)]
extern "C" fn onAsyncErrorCb(
    _codec: *mut AMediaCodec,
    userdata: *mut c_void,
    error: media_status_t,
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
    TimedOut,
}

impl std::fmt::Display for MediaError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}

impl std::error::Error for MediaError {}

impl From<media_status_t> for MediaError {
    fn from(err: media_status_t) -> Self {
        const ENOMEM_ERR: media_status_t = -libc::ENOMEM;
        const ENOENT_ERR: media_status_t = -libc::ENOENT;

        match err {
            1100 => Self::InsufficientResources,
            1101 => Self::Reclaimed,
            ENOMEM_ERR => Self::NoMemory,
            ENOENT_ERR => Self::NotFound,
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
    format: NonNull<AMediaFormat>,
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
    pub unsafe fn from_native(format: NonNull<AMediaFormat>) -> Self {
        Self { format }
    }

    unsafe fn clone_from_native(from: *mut AMediaFormat) -> Result<Self, MediaError> {
        let clone = Self::new()?;

        // SAFETY: "format" and "from" are known to be a valid, non-null pointer.
        let err = unsafe { AMediaFormat_copy(clone.format.as_ptr(), from) };

        if err != 0 {
            return Err(MediaError::from(err));
        }

        Ok(clone)
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
        static TO_STRING_MUTEX: Mutex<()> = Mutex::new(());

        let c_str: CString;

        {
            // AMediaFormat_toString is not thread-safe, the returned char* pointer
            // is only valid until the next call to the API, which might happen on a
            // different thread. Assuming our code is the only code that calls this
            // ... we'll protect against this happening using the mutex.

            let _guard = TO_STRING_MUTEX.lock().unwrap();
            // SAFETY: "format" is known to be a valid, non-null pointer,
            // AMediaFormat_toString returns a valid utf8-encoded, nul-terminated,
            // C string by API contract.
            c_str = unsafe {
                let bytes = AMediaFormat_toString(self.format.as_ptr());
                CStr::from_ptr(bytes).into()
            };
        }

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
    codec: NonNull<AMediaCodec>,

    // "sender_box" holds the reference that's passed to
    // AMediaCodec_setAsyncNotifyCallback as a raw pointer below.
    #[allow(dead_code)]
    sender_box: Pin<Box<Sender<AsyncMessage>>>,

    receiver: Receiver<AsyncMessage>,
}

pub struct InputBuffer<'a> {
    session: &'a Session,
    index: usize,
    data: &'a mut [u8],
    queued: bool,
}

impl<'a> InputBuffer<'a> {
    fn new(session: &'a Session, index: usize) -> Result<Self, MediaError> {
        let data = session.get_input_buffer_data(index)?;

        Ok(Self { session, index, data, queued: false })
    }

    pub fn capacity(&self) -> usize {
        self.data.len()
    }

    pub fn queue(mut self, offset: usize, size: usize, time: u64, flags: u32)
        -> Result<(), MediaError> {

        if offset + size > self.data.len() {
            return Err(MediaError::InvalidParameter);
        }

        self.queued = true;
        self.session.queue_input_buffer(self.index, offset, size, time, flags)
    }
}

impl<'a> AsRef<[u8]> for InputBuffer<'a> {
    fn as_ref(&self) -> &[u8] {
        self.data
    }
}

impl<'a> AsMut<[u8]> for InputBuffer<'a> {
    fn as_mut(&mut self) -> &mut [u8] {
        self.data
    }
}

impl<'a> Drop for InputBuffer<'a> {
    fn drop(&mut self) {
        if !self.queued {
            _ = self.session.input_buffer_dropped(self.index)
        }
    }
}

impl<'a> std::fmt::Debug for InputBuffer<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "InputBuffer(index: {}, capacity: {})", self.index, self.capacity())
    }
}

pub struct OutputBuffer<'a> {
    session: &'a Session,
    index: usize,
    info: AMediaCodecBufferInfo,
    data: &'a [u8],
}

impl<'a> OutputBuffer<'a> {
    fn new(session: &'a Session, index: usize, info: AMediaCodecBufferInfo)
        -> Result<Self, MediaError> {

        let data = if info.size == 0 {
            &[]
        } else {
            session.get_output_buffer_data(index)?
        };

        Ok(Self { session, index, info, data })
    }

    pub fn len(&self) -> usize {
        self.info.size.try_into().unwrap()
    }

    pub fn is_empty(&self) -> bool {
        self.info.size == 0
    }

    pub fn flags(&self) -> u32 {
        self.info.flags
    }

    pub fn offset(&self) -> usize {
        self.info.offset.try_into().unwrap()
    }

    pub fn presentation_time(&self) -> Duration {
        Duration::from_micros(self.info.presentationTimeUs.try_into().unwrap())
    }
}

impl<'a> Drop for OutputBuffer<'a> {
    fn drop(&mut self) {
        _ = self.session.release_output_buffer(self.index, false /* output */);
    }
}

impl<'a> AsRef<[u8]> for OutputBuffer<'a> {
    fn as_ref(&self) -> &[u8] {
        self.data
    }
}

impl<'a> std::fmt::Debug for OutputBuffer<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "OutputBuffer(index: {}, size: {}, flags: {:#x})", self.index, self.info.size, self.info.flags)
    }
}

pub struct Session {
    codec: MediaCodec,
}

impl Session {
    fn new(codec: MediaCodec) -> Self {
        Self { codec }
    }

    pub fn stop(mut self) -> Result<MediaCodec, MediaError> {
        self.codec.stop()?;

        Ok(self.codec)
    }

    pub fn flush(&mut self) -> Result<(), MediaError> {
        self.codec.flush()
    }

    pub fn get_output_format(&self) -> Result<MediaFormat, MediaError> {
        self.codec.get_output_format()
    }

    pub(crate) fn get_input_buffer_data(&self, index: usize)
        -> Result<&mut [u8], MediaError> {

        self.codec.get_input_buffer_data(index)
    }

    pub(crate) fn get_output_buffer_data(&self, index: usize) -> Result<&[u8], MediaError> {
        self.codec.get_output_buffer_data(index)
    }

    pub(crate) fn queue_input_buffer(
        &self,
        index: usize,
        offset: usize,
        size: usize,
        time: u64,
        flags: u32,
    ) -> Result<(), MediaError> {
        self.codec.queue_input_buffer(index, offset, size, time, flags)
    }

    pub(crate) fn input_buffer_dropped(&self, index: usize) -> Result<(), MediaError> {
        // The client dropped the InputBuffer without queueing it back to the codec,
        // this makes no sense, but let's try to deal with it by handing it back to the
        // codec empty, it'll eventually be returned to the client again.

        self.queue_input_buffer(
            index, 0 /* offset */, 0 /* size */, 0 /* time */, 0 /* flags */)
    }

    pub(crate) fn release_output_buffer(&self, index: usize, output: bool)
        -> Result<(), MediaError> {

        self.codec.release_output_buffer(index, output)
    }

    pub fn read_message(&self, timeout: Duration)
        -> Result<AsyncSessionMessage, MediaError> {

        let Some(codec_msg) = self.codec.read_message(timeout) else {
            return Err(MediaError::TimedOut);
        };

        Ok(match codec_msg {
            AsyncMessage::InputBufferAvailable(index) => {
                let index: usize =
                    index.try_into().map_err(|_| MediaError::InvalidParameter)?;

                AsyncSessionMessage::InputBufferAvailable(InputBuffer::new(self, index)?)
            },
            AsyncMessage::OutputBufferAvailable(index, info) => {
                let index: usize =
                    index.try_into().map_err(|_| MediaError::InvalidParameter)?;

                AsyncSessionMessage::OutputBufferAvailable(
                    OutputBuffer::new(self, index, info)?)
            },
            AsyncMessage::Error(err, action_code, detail) => {
                AsyncSessionMessage::Error(err, action_code, detail)
            },
            AsyncMessage::FormatChanged(format) => {
                AsyncSessionMessage::FormatChanged(format)
            },
        })
    }
}

// SAFETY: Android's AMediaCodec is known to be portable across thread boundaries
// and so is this wrapper.
unsafe impl std::marker::Send for MediaCodec {}

// SAFETY: Threads can concurrently call methods on &MediaCodec.
unsafe impl std::marker::Sync for MediaCodec {}

impl MediaCodec {
    pub const BUFFER_FLAG_CODECCONFIG: u32 = 2;
    pub const BUFFER_FLAG_EOS: u32 = 4;

    pub fn decoder_by_type(mime: &str) -> Result<Self, MediaError> {
        let mime = CString::new(mime).map_err(|_| -libc::EINVAL)?;

        let codec =
            // SAFETY: "mime" has just been constructed and verified as a valid,
            // non-null, pointer to a C string.
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

        Ok(Self { codec, sender_box: Box::into_pin(sender_box), receiver })
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

    pub fn start(self) -> Result<Session, MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let result = unsafe { AMediaCodec_start(self.codec.as_ptr()) };

        if result != 0 {
            return Err(MediaError::from(result));
        }

        Ok(Session::new(self))
    }

    pub(crate) fn stop(&mut self) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let result = unsafe { AMediaCodec_stop(self.codec.as_ptr()) };

        if result != 0 {
            return Err(MediaError::from(result));
        }

        Ok(())
    }

    pub(crate) fn flush(&mut self) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let result = unsafe { AMediaCodec_flush(self.codec.as_ptr()) };

        if result != 0 {
            return Err(MediaError::from(result));
        }

        Ok(())
    }

    pub(crate) fn get_output_format(&self) -> Result<MediaFormat, MediaError> {
        let format =
            // SAFETY: "codec" is known to be a valid, non-null pointer.
            NonNull::new(unsafe { AMediaCodec_getOutputFormat(self.codec.as_ptr()) })
            .ok_or(MediaError::NoMemory)?;

        Ok(MediaFormat { format })
    }

    pub(crate) fn get_input_buffer_data(
        &self, index: usize) -> Result<&mut [u8], MediaError> {

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

    pub(crate) fn get_output_buffer_data(&self, index: usize) -> Result<&[u8], MediaError> {
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

    pub(crate) fn queue_input_buffer(
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

    pub(crate) fn release_output_buffer(&self, index: usize, output: bool) -> Result<(), MediaError> {
        // SAFETY: "codec" is known to be a valid, non-null pointer.
        let err =
            unsafe { AMediaCodec_releaseOutputBuffer(self.codec.as_ptr(), index, output as c_int) };

        if err != 0 {
            return Err(MediaError::from(err));
        }

        Ok(())
    }

    pub(crate) fn read_message(&self, timeout: Duration) -> Option<AsyncMessage> {
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
