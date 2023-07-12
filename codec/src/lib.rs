#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/codec_ffi.rs"));

pub mod decode;
pub mod encode;
pub use gvc_common;

pub(crate) const MAX_ADATER_NUM_ONE_VENDER: usize = 4;
