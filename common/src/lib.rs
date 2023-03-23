#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::os::raw::{c_int, c_void};

include!(concat!(env!("OUT_DIR"), "/common_ffi.rs"));

pub type NewEncoderCall = unsafe extern "C" fn(
    device: i32,
    format: i32,
    codecID: i32,
    width: i32,
    height: i32,
    gpu: i32,
) -> *mut c_void;

pub type EncodeCall = unsafe extern "C" fn(
    encoder: *mut c_void,
    data: *mut *mut u8,
    linesize: *mut i32,
    callback: EncodeCallback,
    obj: *mut c_void,
) -> c_int;

pub type DestroyEncoderCall = unsafe extern "C" fn(encoder: *mut c_void) -> c_int;

pub type NewDecoderCall =
    unsafe extern "C" fn(device: i32, format: i32, codecID: i32, gpu: i32) -> *mut c_void;

pub type DecodeCall = unsafe extern "C" fn(
    decoder: *mut ::std::os::raw::c_void,
    data: *mut u8,
    length: i32,
    callback: DecodeCallback,
    obj: *mut ::std::os::raw::c_void,
) -> c_int;

pub type DestroyDecoderCall = unsafe extern "C" fn(decoder: *mut c_void) -> c_int;

pub struct EncodeCalls {
    pub new: NewEncoderCall,
    pub encode: EncodeCall,
    pub destroy: DestroyEncoderCall,
}
pub struct DecodeCalls {
    pub new: NewDecoderCall,
    pub decode: DecodeCall,
    pub destroy: DestroyDecoderCall,
}

pub struct InnerEncodeContext {
    pub device: HWDeviceType,
    pub codec: CodecID,
}

pub struct InnerDecodeContext {
    pub device: HWDeviceType,
    pub codec: CodecID,
}
