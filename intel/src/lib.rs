#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/intel_ffi.rs"));

use hw_common::{
    inner::{DecodeCalls, EncodeCalls, InnerDecodeContext, InnerEncodeContext},
    DataFormat::*,
    API::*,
};

pub fn encode_calls() -> EncodeCalls {
    EncodeCalls {
        new: intel_new_encoder,
        encode: intel_encode,
        destroy: intel_destroy_encoder,
        test: intel_test_encode,
        set_bitrate: intel_set_bitrate,
        set_framerate: intel_set_framerate,
    }
}

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: intel_new_decoder,
        decode: intel_decode,
        destroy: intel_destroy_decoder,
        test: intel_test_decode,
    }
}

pub fn possible_support_encoders() -> Vec<InnerEncodeContext> {
    if unsafe { intel_driver_support() } != 0 {
        return vec![];
    }
    let devices = vec![API_DX11];
    let dataFormats = vec![H264, H265];
    let mut v = vec![];
    for device in devices.iter() {
        for dataFormat in dataFormats.iter() {
            v.push(InnerEncodeContext {
                api: device.clone(),
                format: dataFormat.clone(),
            });
        }
    }
    v
}

pub fn possible_support_decoders() -> Vec<InnerDecodeContext> {
    if unsafe { intel_driver_support() } != 0 {
        return vec![];
    }
    let devices = vec![API_DX11];
    let dataFormats = vec![H264, H265];
    let mut v = vec![];
    for device in devices.iter() {
        for dataFormat in dataFormats.iter() {
            v.push(InnerDecodeContext {
                api: device.clone(),
                dataFormat: dataFormat.clone(),
            });
        }
    }
    v
}
