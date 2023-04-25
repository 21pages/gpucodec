use hw_common::{
    inner::EncodeCalls, DataFormat::*, DynamicContext, EncodeContext, EncodeDriver::*,
    FeatureContext, HWDeviceType::*, PixelFormat::NV12, MAX_DATA_NUM,
};
use log::trace;
use serde_derive::{Deserialize, Serialize};
use std::{
    fmt::Display,
    os::raw::{c_int, c_void},
    slice::from_raw_parts,
    sync::{Arc, Condvar, Mutex},
    thread,
    time::Instant,
};

pub struct Encoder {
    calls: EncodeCalls,
    codec: Box<c_void>,
    frames: *mut Vec<EncodeFrame>,
    pub ctx: EncodeContext,
}

unsafe impl Send for Encoder {}
unsafe impl Sync for Encoder {}

impl Encoder {
    pub fn new(ctx: EncodeContext) -> Result<Self, ()> {
        if ctx.d.width % 2 == 1 || ctx.d.height % 2 == 1 {
            return Err(());
        }
        let calls = match ctx.f.driver {
            NVENC => nvidia::encode_calls(),
            AMF => amf::encode_calls(),
            MFX => intel::encode_calls(),
        };
        unsafe {
            let codec = (calls.new)(
                ctx.d.device,
                ctx.f.device as _,
                ctx.f.dataFormat as i32,
                ctx.d.width,
                ctx.d.height,
                ctx.d.kbitrate,
                ctx.d.framerate,
                ctx.d.gop,
            );
            if codec.is_null() {
                return Err(());
            }
            Ok(Self {
                calls,
                codec: Box::from_raw(codec as *mut c_void),
                frames: Box::into_raw(Box::new(Vec::<EncodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn encode(&mut self, tex: *mut c_void) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let result = (self.calls.encode)(
                &mut *self.codec,
                tex,
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

    pub fn set_bitrate(&mut self, kbs: i32) -> Result<(), i32> {
        unsafe {
            match (self.calls.set_bitrate)(&mut *self.codec, kbs) {
                0 => Ok(()),
                err => Err(err),
            }
        }
    }

    pub fn set_framerate(&mut self, framerate: i32) -> Result<(), i32> {
        unsafe {
            match (self.calls.set_framerate)(&mut *self.codec, framerate) {
                0 => Ok(()),
                err => Err(err),
            }
        }
    }
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

// pub fn available(d: DynamicContext) -> Vec<FeatureContext> {
//     static mut CACHED: Vec<FeatureContext> = vec![];
//     static mut CACHED_INPUT: Option<DynamicContext> = None;
//     unsafe {
//         if CACHED_INPUT.clone().take() != Some(d.clone()) {
//             CACHED_INPUT = Some(d.clone());
//             CACHED = available_(d);
//         }
//     }
//     unsafe { CACHED.clone() }
// }

// fn available_(d: DynamicContext) -> Vec<FeatureContext> {
//     // to-do: disable log
//     let format = NV12;
//     let mut natives: Vec<_> = nvidia::possible_support_encoders()
//         .drain(..)
//         .map(|n| (NVENC, n))
//         .collect();
//     natives.append(
//         &mut amf::possible_support_encoders()
//             .drain(..)
//             .map(|n| (AMF, n))
//             .collect(),
//     );
//     let inputs = natives.drain(..).map(|(driver, n)| EncodeContext {
//         f: FeatureContext {
//             driver,
//             device: n.device,
//             pixfmt: format,
//             dataFormat: n.format,
//         },
//         d,
//     });
//     // https://forums.developer.nvidia.com/t/is-there-limit-for-multi-thread-encoder/73187
//     // https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new
//     let max_nv_thread = 1;
//     let nv_cond = Arc::new((Mutex::new(0), Condvar::new()));
//     let outputs = Arc::new(Mutex::new(Vec::<EncodeContext>::new()));
//     let start = Instant::now();
//     let yuv = vec![0u8; (d.height * d.width * 3 / 2) as usize]; // to-do
//     log::debug!("prepare yuv {:?}", start.elapsed());
//     let yuv = Arc::new(yuv);
//     let mut handles = vec![];
//     for input in inputs {
//         let yuv = yuv.clone();
//         let outputs = outputs.clone();
//         let nv_cond = nv_cond.clone();
//         let handle = thread::spawn(move || {
//             let (nv_cnt, nv_cvar) = &*nv_cond;
//             if input.f.driver == NVENC {
//                 let _guard = nv_cvar
//                     .wait_while(nv_cnt.lock().unwrap(), |cnt| *cnt >= max_nv_thread)
//                     .unwrap();
//             }
//             *nv_cnt.lock().unwrap() += 1;
//             let mut linesizes = vec![d.width, d.width]; // to-do
//             linesizes.resize(8, 0);
//             let ylen = (linesizes[0] * d.height) as usize;
//             let y = &yuv[0..ylen];
//             let uv = &yuv[ylen..];
//             let yuvs = vec![y, uv];
//             let start = Instant::now();
//             if let Ok(mut encoder) = Encoder::new(input.clone()) {
//                 log::debug!("{:?} new {:?}", input, start.elapsed());
//                 let start = Instant::now();
//                 if let Ok(_) = encoder.encode(yuvs, linesizes.clone()) {
//                     log::debug!("{:?} encode {:?}", input, start.elapsed());
//                     outputs.lock().unwrap().push(input.clone());
//                 } else {
//                     log::debug!("{:?} encode failed {:?}", input, start.elapsed());
//                 }
//             } else {
//                 log::debug!("{:?} new failed {:?}", input, start.elapsed());
//             }
//             if input.f.driver == NVENC {
//                 *nv_cnt.lock().unwrap() -= 1;
//                 nv_cvar.notify_one();
//             }
//         });
//         handles.push(handle);
//     }
//     for handle in handles {
//         handle.join().ok();
//     }
//     let mut x = outputs.lock().unwrap().clone();
//     x.drain(..).map(|e| e.f).collect()
// }

// #[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
// pub struct Best {
//     pub h264: Option<FeatureContext>,
//     pub h265: Option<FeatureContext>,
// }

// impl Best {
//     pub fn new(encoders: Vec<FeatureContext>) -> Self {
//         let mut h264s: Vec<_> = encoders
//             .iter()
//             .filter(|e| e.dataFormat == H264)
//             .map(|e| e.to_owned())
//             .collect();
//         let mut h265s: Vec<_> = encoders
//             .iter()
//             .filter(|e| e.dataFormat == H265)
//             .map(|e| e.to_owned())
//             .collect();
//         let sort = |h26xs: &mut Vec<FeatureContext>| {
//             let device_order = vec![CUDA, DX11, DX12, DX9, VULKAN, OPENGL, OPENCL, DX10, HOST];
//             h26xs.sort_by(|a, b| {
//                 let mut index_a = device_order.len();
//                 let mut index_b = device_order.len();
//                 for i in 0..device_order.len() {
//                     if a.device == device_order[i] {
//                         index_a = i;
//                     }
//                     if b.device == device_order[i] {
//                         index_b = i;
//                     }
//                 }
//                 index_a
//                     .partial_cmp(&index_b)
//                     .unwrap_or(std::cmp::Ordering::Equal)
//             });
//         };
//         sort(&mut h264s);
//         sort(&mut h265s);
//         Self {
//             h264: h264s.first().cloned(),
//             h265: h265s.first().cloned(),
//         }
//     }

//     pub fn serialize(&self) -> Result<String, ()> {
//         match serde_json::to_string_pretty(self) {
//             Ok(s) => Ok(s),
//             Err(_) => Err(()),
//         }
//     }

//     pub fn deserialize(s: &str) -> Result<Self, ()> {
//         match serde_json::from_str(s) {
//             Ok(c) => Ok(c),
//             Err(_) => Err(()),
//         }
//     }
// }
