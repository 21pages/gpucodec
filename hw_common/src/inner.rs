use crate::{DataFormat, DecodeCallback, EncodeCallback, HWDeviceType};
use std::os::raw::{c_int, c_void};

pub type NewEncoderCall = unsafe extern "C" fn(
    pDevice: *mut c_void,
    codecID: i32,
    width: i32,
    height: i32,
    bitrate: i32,
    framerate: i32,
    gop: i32,
    pitchs: *mut i32,
) -> *mut c_void;

pub type EncodeCall = unsafe extern "C" fn(
    encoder: *mut c_void,
    tex: *mut c_void,
    callback: EncodeCallback,
    obj: *mut c_void,
) -> c_int;

pub type NewDecoderCall =
    unsafe extern "C" fn(device: i32, format: i32, codecID: i32) -> *mut c_void;

pub type DecodeCall = unsafe extern "C" fn(
    decoder: *mut c_void,
    data: *mut u8,
    length: i32,
    callback: DecodeCallback,
    obj: *mut c_void,
) -> c_int;

pub type IVCall = unsafe extern "C" fn(v: *mut c_void) -> c_int;

pub type IVICall = unsafe extern "C" fn(v: *mut c_void, v: i32) -> c_int;

pub struct EncodeCalls {
    pub new: NewEncoderCall,
    pub encode: EncodeCall,
    pub destroy: IVCall,
    pub set_bitrate: IVICall,
    pub set_framerate: IVICall,
}
pub struct DecodeCalls {
    pub new: NewDecoderCall,
    pub decode: DecodeCall,
    pub destroy: IVCall,
}

pub struct InnerEncodeContext {
    pub device: HWDeviceType,
    pub format: DataFormat,
}

pub struct InnerDecodeContext {
    pub device: HWDeviceType,
    pub dataFormat: DataFormat,
}
