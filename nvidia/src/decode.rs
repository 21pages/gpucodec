use crate::{nvidia_decode, nvidia_destroy_decoder, nvidia_new_decoder};
use common::{decode_callback, DecodeContext, DecodeFrame};
use log::{error, trace};
use std::ffi::c_void;

pub struct NvDecoder {
    codec: Box<c_void>,
    frames: *mut Vec<DecodeFrame>,
    pub ctx: DecodeContext,
}

unsafe impl Send for NvDecoder {}
unsafe impl Sync for NvDecoder {}

impl NvDecoder {
    pub fn new(ctx: DecodeContext) -> Result<Self, ()> {
        unsafe {
            let codec = nvidia_new_decoder(ctx.codec as i32, 0);

            if codec.is_null() {
                return Err(());
            }

            Ok(NvDecoder {
                codec: Box::from_raw(codec as *mut c_void),
                frames: Box::into_raw(Box::new(Vec::<DecodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn decode(&mut self, packet: &[u8]) -> Result<&mut Vec<DecodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let ret = nvidia_decode(
                &mut *self.codec,
                packet.as_ptr() as _,
                packet.len() as _,
                Some(decode_callback),
                self.frames as *mut _ as *mut c_void,
            );

            if ret < 0 {
                error!("Error decode: {}", ret);
                Err(ret)
            } else {
                Ok(&mut *self.frames)
            }
        }
    }
}

impl Drop for NvDecoder {
    fn drop(&mut self) {
        unsafe {
            nvidia_destroy_decoder(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Decoder dropped");
        }
    }
}
