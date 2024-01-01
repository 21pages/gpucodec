#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/mfdx_ffi.rs"));

use gpu_common::{
    inner::{DecodeCalls, InnerDecodeContext},
    DataFormat::*,
    API::*,
};

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: mfdx_new_decoder,
        decode: mfdx_decode,
        destroy: mfdx_destroy_decoder,
        test: mfdx_test_decode,
    }
}

pub fn possible_support_decoders() -> Vec<InnerDecodeContext> {
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
