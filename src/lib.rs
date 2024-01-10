#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/gpucodec_ffi.rs"));

mod amf;
pub mod common;
pub mod decode;
pub mod encode;
mod inner;
mod nv;
mod vpl;

pub(crate) const MAX_ADATER_NUM_ONE_VENDER: usize = 4;
