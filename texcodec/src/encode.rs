use hw_common::{
    inner::EncodeCalls, DynamicContext, EncodeContext, EncodeDriver::*, FeatureContext,
};
use log::trace;
use std::{
    fmt::Display,
    os::raw::{c_int, c_void},
    slice::from_raw_parts,
    sync::{Arc, Mutex},
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
                ctx.d.device.unwrap_or(std::ptr::null_mut()),
                ctx.f.api as _,
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

    pub fn test(&mut self) -> Result<(), i32> {
        unsafe {
            let result = (self.calls.test)(&mut *self.codec);
            if result == 0 {
                Ok(())
            } else {
                Err(result)
            }
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

pub fn available(d: DynamicContext) -> Vec<FeatureContext> {
    available_(d)
}

fn available_(d: DynamicContext) -> Vec<FeatureContext> {
    let mut natives: Vec<_> = vec![];
    natives.append(
        &mut nvidia::possible_support_encoders()
            .drain(..)
            .map(|n| (NVENC, n))
            .collect(),
    );
    natives.append(
        &mut amf::possible_support_encoders()
            .drain(..)
            .map(|n| (AMF, n))
            .collect(),
    );
    natives.append(
        &mut intel::possible_support_encoders()
            .drain(..)
            .map(|n| (MFX, n))
            .collect(),
    );
    let inputs = natives.drain(..).map(|(driver, n)| EncodeContext {
        f: FeatureContext {
            driver,
            api: n.api,
            dataFormat: n.format,
        },
        d,
    });
    let outputs = Arc::new(Mutex::new(Vec::<EncodeContext>::new()));
    let mut handles = vec![];
    for input in inputs {
        let outputs = outputs.clone();
        let handle = thread::spawn(move || {
            let start = Instant::now();
            if let Ok(mut encoder) = Encoder::new(input.clone()) {
                log::debug!("{:?} new {:?}", input, start.elapsed());
                let start = Instant::now();
                if let Ok(_) = encoder.test() {
                    log::debug!("{:?} encode {:?}", input, start.elapsed());
                    outputs.lock().unwrap().push(input.clone());
                } else {
                    log::debug!("{:?} encode failed {:?}", input, start.elapsed());
                }
            } else {
                log::debug!("{:?} new failed {:?}", input, start.elapsed());
            }
        });
        handles.push(handle);
    }
    for handle in handles {
        handle.join().ok();
    }
    let mut x = outputs.lock().unwrap().clone();
    x.drain(..).map(|e| e.f).collect()
}
