#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
use serde_derive::{Deserialize, Serialize};
include!(concat!(env!("OUT_DIR"), "/common_ffi.rs"));

pub mod inner;
pub use serde;
pub use serde_derive;

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub enum EncodeDriver {
    NVENC,
    AMF,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub enum DecodeDriver {
    CUVID,
    AMF,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct StaticContext {
    pub driver: EncodeDriver,
    pub device: HWDeviceType,
    pub pixfmt: PixelFormat,
    pub dataFormat: DataFormat,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
pub struct DynamicContext {
    pub width: i32,
    pub height: i32,
    pub kbitrate: i32,
    pub framerate: i32,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct EncodeContext {
    pub s: StaticContext,
    pub d: DynamicContext,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct DecodeContext {
    pub driver: DecodeDriver,
    pub device: HWDeviceType,
    pub pixfmt: PixelFormat,
    pub dataFormat: DataFormat,
}
