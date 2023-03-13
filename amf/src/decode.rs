use crate::{
    amf_decode, amf_destroy_decoder, amf_new_decoder, Codec, AMF_MEMORY_TYPE, AMF_SURFACE_FORMAT,
    MAX_AV_PLANES,
};
use log::{error, trace};
use std::{ffi::c_void, os::raw::c_int, slice::from_raw_parts, vec};

#[derive(Debug, Clone)]
pub struct DecodeContext {
    pub memory_type: AMF_MEMORY_TYPE,
    pub format_out: AMF_SURFACE_FORMAT,
    pub codec: Codec,
}

pub struct DecodeFrame {
    pub surface_format: AMF_SURFACE_FORMAT,
    pub width: usize,
    pub height: usize,
    pub data: Vec<Vec<u8>>,
    pub linesize: Vec<usize>,
    pub key: bool,
}

impl std::fmt::Display for DecodeFrame {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut s = String::from("data:");
        for data in self.data.iter() {
            s.push_str(format!("{} ", data.len()).as_str());
        }
        s.push_str(", linesize:");
        for linesize in self.linesize.iter() {
            s.push_str(format!("{} ", linesize).as_str());
        }

        write!(
            f,
            "surface_format:{}, width:{}, height:{},key:{}, {}",
            self.surface_format as i32, self.width, self.height, self.key, s,
        )
    }
}

pub struct Decoder {
    codec: Box<c_void>,
    frames: *mut Vec<DecodeFrame>,
    pub ctx: DecodeContext,
}

unsafe impl Send for Decoder {}
unsafe impl Sync for Decoder {}

impl Decoder {
    pub fn new(ctx: DecodeContext) -> Result<Self, ()> {
        unsafe {
            let codec = amf_new_decoder(ctx.memory_type, ctx.format_out, ctx.codec);

            if codec.is_null() {
                return Err(());
            }

            Ok(Decoder {
                codec: Box::from_raw(codec as *mut c_void),
                frames: Box::into_raw(Box::new(Vec::<DecodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn decode(&mut self, packet: &[u8]) -> Result<&mut Vec<DecodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let ret = amf_decode(
                &mut *self.codec,
                packet.as_ptr() as _,
                packet.len() as _,
                Some(Decoder::callback),
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

    unsafe extern "C" fn callback(
        datas: *mut *mut u8,
        linesizes: *mut u32,
        surfaceFormat: c_int,
        width: c_int,
        height: c_int,
        obj: *const c_void,
        key: c_int,
    ) {
        let frames = &mut *(obj as *mut Vec<DecodeFrame>);
        let datas = from_raw_parts(datas, MAX_AV_PLANES as _);
        let linesizes = from_raw_parts(linesizes, MAX_AV_PLANES as _);

        let mut frame = DecodeFrame {
            surface_format: std::mem::transmute(surfaceFormat),
            width: width as _,
            height: height as _,
            data: vec![],
            linesize: vec![],
            key: key != 0,
        };

        if surfaceFormat == AMF_SURFACE_FORMAT::AMF_SURFACE_YUV420P as c_int {
            let y = from_raw_parts(datas[0], (linesizes[0] * height as u32) as usize).to_vec();
            let u = from_raw_parts(datas[1], (linesizes[1] * height as u32 / 2) as usize).to_vec();
            let v = from_raw_parts(datas[2], (linesizes[2] * height as u32 / 2) as usize).to_vec();

            frame.data.push(y);
            frame.data.push(u);
            frame.data.push(v);

            frame.linesize.push(linesizes[0] as _);
            frame.linesize.push(linesizes[1] as _);
            frame.linesize.push(linesizes[2] as _);

            frames.push(frame);
        } else if surfaceFormat == AMF_SURFACE_FORMAT::AMF_SURFACE_NV12 as c_int {
            let y = from_raw_parts(datas[0], (linesizes[0] * height as u32) as usize).to_vec();
            let uv = from_raw_parts(datas[1], (linesizes[1] * height as u32 / 2) as usize).to_vec();

            frame.data.push(y);
            frame.data.push(uv);

            frame.linesize.push(linesizes[0] as _);
            frame.linesize.push(linesizes[1] as _);

            frames.push(frame);
        } else {
            error!("unsupported pixfmt {}", surfaceFormat);
        }
    }
}

impl Drop for Decoder {
    fn drop(&mut self) {
        unsafe {
            amf_destroy_decoder(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Decoder dropped");
        }
    }
}
