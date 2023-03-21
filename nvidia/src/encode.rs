use crate::{nvidia_destroy_encoder, nvidia_encode, nvidia_new_encoder};
use common::{encode_callback, EncodeContext, EncodeFrame, MAX_DATA_NUM};
use log::trace;
use std::{ffi::c_void, os::raw::c_int};

pub struct NvEncoder {
    codec: Box<c_void>,
    frames: *mut Vec<EncodeFrame>,
    pub ctx: EncodeContext,
    pub linesize: Vec<i32>,
    pub offset: Vec<i32>,
    pub length: i32,
}

impl NvEncoder {
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
                ctx.format as u32,
                0,
            );
            if codec.is_null() {
                return Err(());
            }
            Ok(NvEncoder {
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
                Some(encode_callback),
                self.frames as *mut _ as *mut c_void,
            );
            if result != 0 {
                Err(result)
            } else {
                Ok(&mut *self.frames)
            }
        }
    }
}

impl Drop for NvEncoder {
    fn drop(&mut self) {
        unsafe {
            nvidia_destroy_encoder(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Encoder dropped");
        }
    }
}
