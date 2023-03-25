use codec_common::{
    inner::EncodeCalls, DataFormat::*, EncodeContext, EncodeDriver::*, PixelFormat::NV12,
    MAX_DATA_NUM,
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
        let calls = match ctx.driver {
            NVENC => nvidia::encode_calls(),
            AMF => amf::encode_calls(),
        };
        unsafe {
            let codec = (calls.new)(
                ctx.device as i32,
                ctx.pixfmt as i32,
                ctx.dataFormat as i32,
                ctx.width,
                ctx.height,
                ctx.gpu,
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

    pub fn encode(
        &mut self,
        datas: Vec<&[u8]>,
        linesizes: Vec<i32>,
    ) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            let mut datas = datas;
            let mut linesizes = linesizes;
            let mut datas: Vec<*const u8> = datas.drain(..).map(|d| d.as_ptr()).collect();
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

pub fn available() -> Vec<EncodeContext> {
    use std::sync::Once;
    static mut CACHED: Vec<EncodeContext> = vec![];
    static ONCE: Once = Once::new();
    ONCE.call_once(|| unsafe {
        CACHED = available_();
    });
    unsafe { CACHED.clone() }
}

fn available_() -> Vec<EncodeContext> {
    // to-do: disable log
    let width = 1920;
    let height = 1080;
    let gpu = 0;
    let format = NV12;
    let mut natives: Vec<_> = nvidia::possible_support_encoders()
        .drain(..)
        .map(|n| (NVENC, n))
        .collect();
    natives.append(
        &mut amf::possible_support_encoders()
            .drain(..)
            .map(|n| (AMF, n))
            .collect(),
    );
    let inputs = natives.drain(..).map(|(driver, n)| EncodeContext {
        driver,
        device: n.device,
        pixfmt: format,
        dataFormat: n.format,
        width,
        height,
        gpu,
    });
    // https://forums.developer.nvidia.com/t/is-there-limit-for-multi-thread-encoder/73187
    // https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new
    let max_nv_thread = 1;
    let nv_cond = Arc::new((Mutex::new(0), Condvar::new()));
    let outputs = Arc::new(Mutex::new(Vec::<EncodeContext>::new()));
    let start = Instant::now();
    let yuv = vec![0u8; (height * width * 3 / 2) as usize];
    log::debug!("prepare yuv {:?}", start.elapsed());
    let yuv = Arc::new(yuv);
    let mut handles = vec![];
    for input in inputs {
        let yuv = yuv.clone();
        let outputs = outputs.clone();
        let nv_cond = nv_cond.clone();
        let handle = thread::spawn(move || {
            let (nv_cnt, nv_cvar) = &*nv_cond;
            if input.driver == NVENC {
                let _guard = nv_cvar
                    .wait_while(nv_cnt.lock().unwrap(), |cnt| *cnt >= max_nv_thread)
                    .unwrap();
            }
            *nv_cnt.lock().unwrap() += 1;
            let mut linesizes = vec![1920, 1920];
            linesizes.resize(8, 0);
            let ylen = (linesizes[0] * height) as usize;
            let y = &yuv[0..ylen];
            let uv = &yuv[ylen..];
            let yuvs = vec![y, uv];
            let start = Instant::now();
            if let Ok(mut encoder) = Encoder::new(input.clone()) {
                log::debug!("{:?} new {:?}", input, start.elapsed());
                let start = Instant::now();
                if let Ok(_) = encoder.encode(yuvs, linesizes.clone()) {
                    log::debug!("{:?} encode {:?}", input, start.elapsed());
                    outputs.lock().unwrap().push(input.clone());
                } else {
                    log::debug!("{:?} encode failed {:?}", input, start.elapsed());
                }
            } else {
                log::debug!("{:?} new failed {:?}", input, start.elapsed());
            }
            if input.driver == NVENC {
                *nv_cnt.lock().unwrap() -= 1;
                nv_cvar.notify_one();
            }
        });
        handles.push(handle);
    }
    for handle in handles {
        handle.join().ok();
    }
    let x = outputs.lock().unwrap().clone();
    x
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct Best {
    pub h264: Option<EncodeContext>,
    pub hevc: Option<EncodeContext>,
}

impl Best {
    pub fn new(encoders: Vec<EncodeContext>) -> Self {
        let h264s: Vec<_> = encoders
            .iter()
            .filter(|e| e.dataFormat == H264)
            .map(|e| e.to_owned())
            .collect();
        let hevcs: Vec<_> = encoders
            .iter()
            .filter(|e| e.dataFormat == HEVC)
            .map(|e| e.to_owned())
            .collect();
        Self {
            h264: h264s.first().cloned(),
            hevc: hevcs.first().cloned(),
        }
    }

    pub fn serialize(&self) -> Result<String, ()> {
        match serde_json::to_string_pretty(self) {
            Ok(s) => Ok(s),
            Err(_) => Err(()),
        }
    }

    pub fn deserialize(s: &str) -> Result<Self, ()> {
        match serde_json::from_str(s) {
            Ok(c) => Ok(c),
            Err(_) => Err(()),
        }
    }
}
