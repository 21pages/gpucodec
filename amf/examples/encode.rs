use amf::{
    encode::{EncodeContext, Encoder},
    Codec::*,
    AMF_MEMORY_TYPE::*,
    AMF_SURFACE_FORMAT::*,
};
use std::{
    io::{Read, Write},
    time::{Duration, Instant},
};

// cargo run --package amf --example encode --release

fn main() {
    let ctx = EncodeContext {
        memoryType: AMF_MEMORY_DX11, //DX9 got Segmentation fault
        surfaceFormat: AMF_SURFACE_NV12,
        codec: H264,
        width: 2880,
        height: 1800,
    };
    let mut encoder = Encoder::new(ctx.clone()).unwrap();
    let mut yuv_file = std::fs::File::open("D:\\tmp\\2880x1800_nv12.yuv").unwrap();
    let len = 7776000;
    let mut buf = vec![0u8; len];
    let mut sum = Duration::ZERO;
    let mut h264_file = std::fs::File::create("D:\\tmp\\tmp.264").unwrap();
    let mut counter = 0;
    unsafe {
        for _ in 0..100 {
            yuv_file.read(&mut buf).unwrap();
            let start = Instant::now();
            let linesizes: Vec<usize> = vec![2880, 2880];
            let y = buf.as_ptr();
            let uv = y.add((linesizes[0] * ctx.height as usize) as _);
            let datas = vec![y, uv];
            let data = encoder.encode(datas, linesizes).unwrap();
            sum += start.elapsed();
            counter += data.len();
            for f in data {
                h264_file.write_all(&f.data).unwrap();
            }
        }
        println!("avg:{:?}, counter:{}", sum / 100, counter);
    }
}
