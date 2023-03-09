use crate::{
    amf_encode, amf_new_encoder,
    AMF_SURFACE_FORMAT::{self, *},
    MAX_AV_PLANES,
};
use log::{error, trace};
use std::{
    ffi::{c_void, CString},
    fmt::Display,
    os::raw::c_int,
    slice,
    sync::{Arc, Mutex},
    thread,
    time::Instant,
};

#[derive(Debug, Clone, PartialEq)]
pub struct EncodeContext {
    pub name: String,
    pub width: i32,
    pub height: i32,
    pub pixfmt: AMF_SURFACE_FORMAT,
    pub align: i32,
    pub bitrate: i32,
    pub timebase: [i32; 2],
    pub gop: i32,
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
    start: Instant,
}

impl Encoder {
    pub fn new(ctx: EncodeContext) -> Result<Self, ()> {
        unsafe {
            let mut linesize = Vec::<i32>::new();
            linesize.resize(MAX_AV_PLANES as _, 0);
            let mut offset = Vec::<i32>::new();
            offset.resize(MAX_AV_PLANES as _, 0);
            let mut length = Vec::<i32>::new();
            length.resize(1, 0);
            let codec = amf_new_encoder(
                // CString::new(ctx.name.as_str()).map_err(|_| ())?.as_ptr(),
                ctx.width as _,
                ctx.height as _,
                ctx.pixfmt,
                // ctx.align,
                // ctx.bitrate as _,
                // ctx.timebase[0],
                // ctx.timebase[1],
                // ctx.gop,
                // ctx.quality as _,
                // ctx.rc as _,
                // linesize.as_mut_ptr(),
                // offset.as_mut_ptr(),
                // length.as_mut_ptr(),
                // Some(Encoder::callback),
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
                start: Instant::now(),
            })
        }
    }

    pub fn encode(&mut self, data: &[u8]) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            // let mut linesize = vec![2880, 1440, 1440];
            let mut linesize = vec![2880, 2880];
            linesize.resize(MAX_AV_PLANES as _, 0);
            let y = data.as_ptr();
            let uv = y.add((linesize[0] * self.ctx.height) as _);
            // let u = y.add((linesize[0] * self.ctx.height) as _);
            // let v = u.add((linesize[1] * self.ctx.height) as _);
            // let mut data_array = vec![y, u, v];
            let mut data_array = vec![y, uv];
            data_array.resize(MAX_AV_PLANES as _, std::ptr::null());
            (&mut *self.frames).clear();
            let result = amf_encode(
                &mut *self.codec,
                data_array.as_mut_ptr() as _,
                linesize.as_mut_ptr() as _,
                // (*data).as_ptr(),
                // data.len() as _,
                Some(Encoder::callback),
                self.frames as *mut _ as *mut c_void,
                // self.start.elapsed().as_millis() as _,
            );
            Ok(&mut *self.frames)
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

    // pub fn set_bitrate(&mut self, bitrate: i32) -> Result<(), ()> {
    //     let ret = unsafe { crate::set_bitrate(&mut *self.codec, bitrate) };
    //     if ret == 0 {
    //         Ok(())
    //     } else {
    //         Err(())
    //     }
    // }

    // pub fn format_from_name(name: String) -> Result<DataFormat, ()> {
    //     if name.contains("h264") {
    //         return Ok(H264);
    //     } else if name.contains("hevc") {
    //         return Ok(H265);
    //     }
    //     Err(())
    // }
}

impl Drop for Encoder {
    fn drop(&mut self) {
        unsafe {
            // free_encoder(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Encoder dropped");
        }
    }
}
