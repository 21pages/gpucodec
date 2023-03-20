use crate::{nvidia_decode, nvidia_destroy_decoder, nvidia_new_decoder};
use common::{CodecID, HWDeviceType, PixelFormat, MAX_DATA_NUM};
use log::{error, trace};
use std::{ffi::c_void, os::raw::c_int, slice::from_raw_parts, vec};

#[derive(Debug, Clone)]
pub struct DecodeContext {
    pub device: HWDeviceType,
    pub format_out: PixelFormat,
    pub codec: CodecID,
}

pub struct DecodeFrame {
    pub surface_format: PixelFormat,
    pub width: i32,
    pub height: i32,
    pub data: Vec<Vec<u8>>,
    pub linesize: Vec<i32>,
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
            "surface_format:{:?}, width:{}, height:{},key:{}, {}",
            self.surface_format, self.width, self.height, self.key, s,
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
            let codec = nvidia_new_decoder(ctx.codec as i32, 0);

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
            let ret = nvidia_decode(
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
        linesizes: *mut i32,
        surfaceFormat: c_int,
        width: c_int,
        height: c_int,
        obj: *const c_void,
        key: c_int,
    ) {
        let frames = &mut *(obj as *mut Vec<DecodeFrame>);
        let datas = from_raw_parts(datas, MAX_DATA_NUM as _);
        let linesizes = from_raw_parts(linesizes, MAX_DATA_NUM as _);

        let mut frame = DecodeFrame {
            surface_format: std::mem::transmute(surfaceFormat),
            width: width as _,
            height: height as _,
            data: vec![],
            linesize: vec![],
            key: key != 0,
        };

        if surfaceFormat == PixelFormat::YUV420P as c_int {
            let y = from_raw_parts(datas[0], (linesizes[0] * height) as usize).to_vec();
            let u = from_raw_parts(datas[1], (linesizes[1] * height / 2) as usize).to_vec();
            let v = from_raw_parts(datas[2], (linesizes[2] * height / 2) as usize).to_vec();

            frame.data.push(y);
            frame.data.push(u);
            frame.data.push(v);

            frame.linesize.push(linesizes[0]);
            frame.linesize.push(linesizes[1]);
            frame.linesize.push(linesizes[2]);

            frames.push(frame);
        } else if surfaceFormat == PixelFormat::NV12 as c_int {
            let y = from_raw_parts(datas[0], (linesizes[0] * height) as usize).to_vec();
            let uv = from_raw_parts(datas[1], (linesizes[1] * height / 2) as usize).to_vec();

            frame.data.push(y);
            frame.data.push(uv);

            frame.linesize.push(linesizes[0]);
            frame.linesize.push(linesizes[1]);

            frames.push(frame);
        } else {
            error!("unsupported pixfmt {}", surfaceFormat);
        }
    }
}

impl Drop for Decoder {
    fn drop(&mut self) {
        unsafe {
            nvidia_destroy_decoder(self.codec.as_mut());
            let _ = Box::from_raw(self.frames);
            trace!("Decoder dropped");
        }
    }
}
