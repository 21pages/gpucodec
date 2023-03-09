use amf::{
    encode::{EncodeContext, EncodeFrame, Encoder},
    AMF_SURFACE_FORMAT::*,
};
use std::{
    io::{Read, Write},
    time::{Duration, Instant},
};

// cargo run --package amf --example encode --release

fn main() {
    let ctx = EncodeContext {
        name: "".to_string(),
        width: 2880,
        height: 1800,
        pixfmt: AMF_SURFACE_NV12,
        align: 0,
        bitrate: 0,
        timebase: [0, 0],
        gop: 0,
    };
    let mut encoder = Encoder::new(ctx).unwrap();
    let mut yuv_file = std::fs::File::open("D:\\tmp\\nv12.yuv").unwrap();
    let len = 7776000;
    let mut buf = vec![0u8; len];
    let mut sum = Duration::ZERO;
    let mut h264_file = std::fs::File::create("D:\\tmp\\111.264").unwrap();
    let mut counter = 0;
    for _ in 0..100 {
        yuv_file.read(&mut buf).unwrap();
        let start = Instant::now();
        let data = encoder.encode(&buf).unwrap();
        sum += start.elapsed();
        counter += data.len();
        for f in data {
            h264_file.write_all(&f.data).unwrap();
        }
    }
    println!("avg:{:?}, counter:{}", sum / 100, counter);
}
