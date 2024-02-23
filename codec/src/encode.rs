use gpu_common::{
    inner::EncodeCalls, AdapterDesc, DynamicContext, EncodeContext, EncodeDriver::*, FeatureContext,
};
use log::trace;
use std::{
    fmt::Display,
    os::raw::{c_int, c_void},
    slice::from_raw_parts,
    sync::{Arc, Mutex},
    thread,
};

pub struct Encoder {
    calls: EncodeCalls,
    codec: *mut c_void,
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
            NVENC => nv::encode_calls(),
            AMF => amf::encode_calls(),
            VPL => vpl::encode_calls(),
        };
        unsafe {
            let codec = (calls.new)(
                ctx.d.device.unwrap_or(std::ptr::null_mut()),
                ctx.f.luid,
                ctx.f.api as _,
                ctx.f.data_format as i32,
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
                codec,
                frames: Box::into_raw(Box::new(Vec::<EncodeFrame>::new())),
                ctx,
            })
        }
    }

    pub fn encode(&mut self, tex: *mut c_void) -> Result<&mut Vec<EncodeFrame>, i32> {
        unsafe {
            (&mut *self.frames).clear();
            let result = (self.calls.encode)(
                self.codec,
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

    extern "C" fn callback(data: *const u8, size: c_int, key: i32, obj: *const c_void) {
        unsafe {
            let frames = &mut *(obj as *mut Vec<EncodeFrame>);
            frames.push(EncodeFrame {
                data: from_raw_parts(data, size as usize).to_vec(),
                pts: 0,
                key,
            });
        }
    }

    pub fn set_bitrate(&mut self, kbs: i32) -> Result<(), i32> {
        unsafe {
            match (self.calls.set_bitrate)(self.codec, kbs) {
                0 => Ok(()),
                err => Err(err),
            }
        }
    }

    pub fn set_framerate(&mut self, framerate: i32) -> Result<(), i32> {
        unsafe {
            match (self.calls.set_framerate)(self.codec, framerate) {
                0 => Ok(()),
                err => Err(err),
            }
        }
    }
}

impl Drop for Encoder {
    fn drop(&mut self) {
        unsafe {
            (self.calls.destroy)(self.codec);
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
        write!(f, "encode len:{}, key:{}", self.data.len(), self.key)
    }
}

pub fn available(d: DynamicContext) -> Vec<FeatureContext> {
    let mut natives: Vec<_> = vec![];
    natives.append(
        &mut nv::possible_support_encoders()
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
        &mut vpl::possible_support_encoders()
            .drain(..)
            .map(|n| (VPL, n))
            .collect(),
    );
    let inputs = natives.drain(..).map(|(driver, n)| EncodeContext {
        f: FeatureContext {
            driver,
            api: n.api,
            data_format: n.format,
            luid: 0,
        },
        d,
    });
    let outputs = Arc::new(Mutex::new(Vec::<EncodeContext>::new()));
    let mut handles = vec![];
    for input in inputs {
        let outputs = outputs.clone();
        let handle = thread::spawn(move || {
            let test = match input.f.driver {
                NVENC => nv::encode_calls().test,
                AMF => amf::encode_calls().test,
                VPL => vpl::encode_calls().test,
            };
            let mut descs: Vec<AdapterDesc> = vec![];
            descs.resize(crate::MAX_ADATER_NUM_ONE_VENDER, unsafe {
                std::mem::zeroed()
            });
            let mut desc_count: i32 = 0;
            if 0 == unsafe {
                test(
                    descs.as_mut_ptr() as _,
                    descs.len() as _,
                    &mut desc_count,
                    input.f.api as _,
                    input.f.data_format as i32,
                    input.d.width,
                    input.d.height,
                    input.d.kbitrate,
                    input.d.framerate,
                    input.d.gop,
                )
            } {
                if desc_count as usize <= descs.len() {
                    for i in 0..desc_count as usize {
                        let mut input = input.clone();
                        input.f.luid = descs[i].luid;
                        outputs.lock().unwrap().push(input);
                    }
                }
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
