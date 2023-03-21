use common::{decode_callback, DecodeCalls, DecodeContext, DecodeDriver::*, DecodeFrame};
use log::{error, trace};
use std::ffi::c_void;

pub struct Decoder {
    calls: DecodeCalls,
    codec: Box<c_void>,
    frames: *mut Vec<DecodeFrame>,
    pub ctx: DecodeContext,
}

unsafe impl Send for Decoder {}
unsafe impl Sync for Decoder {}

impl Decoder {
    pub fn new(ctx: DecodeContext) -> Result<Self, ()> {
        let calls = match ctx.driver {
            CUVID => nvidia::decode_calls(),
            AMF => amf::decode_calls(),
        };
        unsafe {
            let codec = (calls.new)(
                ctx.device as i32,
                ctx.format as i32,
                ctx.codec as i32,
                ctx.gpu,
            );
            if codec.is_null() {
                return Err(());
            }
            Ok(Self {
                calls,
                codec: Box::from_raw(codec as *mut c_void),
                frames: Box::into_raw(Box::new(Vec::<DecodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn decode(&mut self, packet: &[u8]) -> Result<&mut Vec<DecodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let ret = (self.calls.decode)(
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

impl Drop for Decoder {
    fn drop(&mut self) {
        unsafe {
            (self.calls.destroy)(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Decoder dropped");
        }
    }
}
