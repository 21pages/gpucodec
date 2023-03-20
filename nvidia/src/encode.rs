use crate::{nvidia_destroy_encoder, nvidia_encode, nvidia_new_encoder, NV_ENC_BUFFER_FORMAT};
use common::{CodecID, HWDeviceType, PixelFormat, MAX_DATA_NUM};
use log::trace;
use std::{ffi::c_void, fmt::Display, os::raw::c_int, slice};

#[derive(Debug, Clone, PartialEq)]
pub struct EncodeContext {
    // pub memoryType: AMF_MEMORY_TYPE,
    pub surfaceFormat: NV_ENC_BUFFER_FORMAT,
    pub codec: CodecID,
    pub width: i32,
    pub height: i32,
}

pub struct EncodeFrame {
    pub data: Vec<u8>,
    pub pts: i64,
    pub key: i32,
}

impl Display for EncodeFrame {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "encode len:{}, pts:{}", self.data.len(), self.pts)
    }
}

pub struct Encoder {
    codec: Box<c_void>,
    frames: *mut Vec<EncodeFrame>,
    pub ctx: EncodeContext,
    pub linesize: Vec<i32>,
    pub offset: Vec<i32>,
    pub length: i32,
}

impl Encoder {
    pub fn new(ctx: EncodeContext) -> Result<Self, ()> {
        unsafe {
            let mut linesize = Vec::<i32>::new();
            linesize.resize(MAX_DATA_NUM as _, 0);
            let mut offset = Vec::<i32>::new();
            offset.resize(MAX_DATA_NUM as _, 0);
            let mut length = Vec::<i32>::new();
            length.resize(1, 0);
            let codec = nvidia_new_encoder(
                // ctx.memoryType,
                ctx.width,
                ctx.height,
                ctx.codec as c_int,
                ctx.surfaceFormat,
                0,
            );
            if codec.is_null() {
                return Err(());
            }
            Ok(Encoder {
                codec: Box::from_raw(codec as *mut c_void),
                frames: Box::into_raw(Box::new(Vec::<EncodeFrame>::new())),
                ctx,
                linesize,
                offset,
                length: length[0],
            })
        }
    }

    pub fn encode(&mut self, data: &[u8]) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let result = nvidia_encode(
                &mut *self.codec,
                data.as_ptr() as *mut u8,
                data.len() as i32,
                Some(Encoder::callback),
                self.frames as *mut _ as *mut c_void,
            );
            if result != 0 {
                Err(result)
            } else {
                Ok(&mut *self.frames)
            }
        }
    }

    extern "C" fn callback(data: *const u8, size: c_int, pts: i64, key: i32, obj: *const c_void) {
        unsafe {
            let frames = &mut *(obj as *mut Vec<EncodeFrame>);
            frames.push(EncodeFrame {
                data: slice::from_raw_parts(data, size as _).to_vec(),
                pts,
                key,
            });
        }
    }
}

impl Drop for Encoder {
    fn drop(&mut self) {
        unsafe {
            nvidia_destroy_encoder(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Encoder dropped");
        }
    }
}
