#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
use std::ffi::c_void;

use serde_derive::{Deserialize, Serialize};
include!(concat!(env!("OUT_DIR"), "/common_ffi.rs"));

pub mod inner;
pub use serde;
pub use serde_derive;

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub enum EncodeDriver {
    NVENC,
    AMF,
    MFX,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub enum DecodeDriver {
    CUVID,
    AMF,
    MFX,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct FeatureContext {
    pub driver: EncodeDriver,
    pub api: API,
    pub dataFormat: DataFormat,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
pub struct DynamicContext {
    #[serde(skip)]
    pub device: Option<*mut c_void>,
    pub width: i32,
    pub height: i32,
    pub kbitrate: i32,
    pub framerate: i32,
    pub gop: i32,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct EncodeContext {
    pub f: FeatureContext,
    pub d: DynamicContext,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
pub struct DecodeContext {
    #[serde(skip)]
    pub device: Option<*mut c_void>,
    pub driver: DecodeDriver,
    pub api: API,
    pub dataFormat: DataFormat,
    pub outputSurfaceFormat: SurfaceFormat,
}
