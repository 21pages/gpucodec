use crate::{amf_destroy_encoder, amf_encode, amf_new_encoder};
use common::{CodecID, HWDeviceType, PixelFormat, MAX_DATA_NUM};
use log::trace;
use std::{ffi::c_void, fmt::Display, os::raw::c_int, slice};

#[derive(Debug, Clone, PartialEq)]
pub struct EncodeContext {
    pub device: HWDeviceType,
    pub format: PixelFormat,
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
            let codec = amf_new_encoder(
                ctx.device as i32,
                ctx.format as i32,
                ctx.codec as i32,
                ctx.width,
                ctx.height,
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

    pub fn encode(
        &mut self,
        datas: Vec<*const u8>,
        linesizes: Vec<i32>,
    ) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            let mut datas = datas;
            let mut linesizes = linesizes;
            datas.resize(MAX_DATA_NUM as _, std::ptr::null());
            linesizes.resize(MAX_DATA_NUM as _, 0);
            (&mut *self.frames).clear();
            let result = amf_encode(
                &mut *self.codec,
                datas.as_ptr() as _,
                linesizes.as_ptr() as _,
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
            amf_destroy_encoder(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Encoder dropped");
        }
    }
}
