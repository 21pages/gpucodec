use codec_common::{DataFormat, EncodeContext, EncodeDriver, HWDeviceType, PixelFormat};
use env_logger::{init_from_env, Env, DEFAULT_FILTER_ENV};
use ffmpeg::mux::{MuxContext, Muxer};
use hwcodec::encode::Encoder;
use std::{io::Read, path::PathBuf, time::Instant};

fn main() {
    init_from_env(Env::default().filter_or(DEFAULT_FILTER_ENV, "info"));
    let input_dir = PathBuf::from("D:\\tmp\\input");
    let output_dir = PathBuf::from("D:\\tmp");
    let yuv_file_name = input_dir.join("2880x1800_nv12.yuv");
    let mut yuv_file = std::fs::File::open(yuv_file_name).unwrap();
    let en_ctx = EncodeContext {
        driver: EncodeDriver::AMF,
        device: HWDeviceType::DX11,
        pixfmt: PixelFormat::NV12,
        dataFormat: DataFormat::H265,
        width: 2880,
        height: 1800,
        gpu: 0,
    };
    let mut_ctx = MuxContext {
        filename: output_dir.join("output.mp4").to_string_lossy().to_string(),
        width: 1920,
        height: 1080,
        is265: en_ctx.dataFormat == DataFormat::H265,
        framerate: 30,
    };
    let mut muxer = Muxer::new(mut_ctx).unwrap();
    let mut encoder = Encoder::new(en_ctx.clone()).unwrap();
    let yuv_len = (en_ctx.width * en_ctx.height * 3 / 2) as usize;
    let mut yuv = vec![0u8; yuv_len];
    let start = Instant::now();
    for _ in 0..100 {
        yuv_file.read(&mut yuv).unwrap();
        let linesizes: Vec<i32> = vec![en_ctx.width, en_ctx.width];
        let ysize = (linesizes[0] * en_ctx.height) as usize;
        let y = &yuv[0..ysize];
        let uv = &yuv[ysize..];
        let datas = vec![y, uv];
        let data = encoder.encode(datas, linesizes).unwrap();
        for f in data {
            muxer.write_video(&f.data, f.key == 1).unwrap();
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
    }
    muxer.write_tail().unwrap();
    log::info!("video time should be {:?}", start.elapsed());
}
