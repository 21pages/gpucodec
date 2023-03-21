use common::{CodecID, EncodeCalls, HWDeviceType, PixelFormat, MAX_DATA_NUM};
use log::trace;
use std::{
    fmt::Display,
    os::raw::{c_int, c_void},
    slice::from_raw_parts,
};
use EncodeDriver::*;

pub struct Encoder {
    calls: EncodeCalls,
    codec: Box<c_void>,
    frames: *mut Vec<EncodeFrame>,
    pub ctx: EncodeContext,
    pub linesize: Vec<i32>,
    pub offset: Vec<i32>,
    pub length: i32,
}

unsafe impl Send for Encoder {}
unsafe impl Sync for Encoder {}

impl Encoder {
    pub fn new(ctx: EncodeContext) -> Result<Self, ()> {
        let calls = match ctx.driver {
            NVENC => nvidia::encode_calls(),
            AMF => amf::encode_calls(),
        };
        // let calls = if ctx.driver == AMF {
        //     amf::encode_calls()
        // } else {
        //     return Err(());
        // };
        unsafe {
            let mut linesize = Vec::<i32>::new();
            linesize.resize(MAX_DATA_NUM as _, 0);
            let mut offset = Vec::<i32>::new();
            offset.resize(MAX_DATA_NUM as _, 0);
            let mut length = Vec::<i32>::new();
            length.resize(1, 0);
            let codec = (calls.new)(
                ctx.device as i32,
                ctx.format as i32,
                ctx.codec as i32,
                ctx.width,
                ctx.height,
            );
            if codec.is_null() {
                return Err(());
            }
            Ok(Self {
                calls,
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
            let result = (self.calls.encode)(
                &mut *self.codec,
                datas.as_ptr() as *mut *mut u8,
                linesizes.as_ptr() as *mut i32,
                Some(Self::callback),
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
                data: from_raw_parts(data, size as usize).to_vec(),
                pts,
                key,
            });
        }
    }

    pub fn available_encoders() {}
}

impl Drop for Encoder {
    fn drop(&mut self) {
        unsafe {
            (self.calls.destroy)(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Encoder dropped");
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum EncodeDriver {
    NVENC,
    AMF,
}

#[derive(Debug, Clone, PartialEq)]
pub struct EncodeContext {
    pub driver: EncodeDriver,
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
