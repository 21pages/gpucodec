#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!(concat!(env!("OUT_DIR"), "/amf_ffi.rs"));

use codec_common::{
    inner::{DecodeCalls, EncodeCalls, InnerDecodeContext, InnerEncodeContext},
    DataFormat::*,
    HWDeviceType::*,
};

pub fn encode_calls() -> EncodeCalls {
    EncodeCalls {
        new: amf_new_encoder,
        encode: amf_encode,
        destroy: amf_destroy_encoder,
        set_bitrate: amf_set_bitrate,
        set_framerate: amf_set_framerate,
    }
}

pub fn decode_calls() -> DecodeCalls {
    DecodeCalls {
        new: amf_new_decoder,
        decode: amf_decode,
        destroy: amf_destroy_decoder,
    }
}

// to-do: hardware ability
pub fn possible_support_encoders() -> Vec<InnerEncodeContext> {
    if unsafe { amf_driver_support() } != 0 {
        return vec![];
    }
    let mut devices = vec![];
    #[cfg(windows)]
    devices.append(&mut vec![DX11, OPENCL]);
    #[cfg(target_os = "linux")]
    devices.append(&mut vec![VULKAN, OPENCL]);
    let codecs = vec![H264, H265];

    let mut v = vec![];
    for device in devices.iter() {
        for codec in codecs.iter() {
            v.push(InnerEncodeContext {
                device: device.clone(),
                format: codec.clone(),
            });
        }
    }
    v
}

pub fn possible_support_decoders() -> Vec<InnerDecodeContext> {
    if unsafe { amf_driver_support() } != 0 {
        return vec![];
    }
    let mut devices = vec![];
    #[cfg(windows)]
    devices.append(&mut vec![DX11, DX12, OPENCL, OPENGL, VULKAN]);
    #[cfg(target_os = "linux")]
    devices.append(&mut vec![VULKAN, OPENCL, OPENGL]);
    let codecs = vec![H264, H265];

    let mut v = vec![];
    for device in devices.iter() {
        for codec in codecs.iter() {
            v.push(InnerDecodeContext {
                device: device.clone(),
                dataFormat: codec.clone(),
            });
        }
    }
    v
}
