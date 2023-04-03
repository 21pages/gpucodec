use hw_common::{
    DataFormat, DecodeContext, DecodeDriver, EncodeContext, EncodeDriver, HWDeviceType, PixelFormat,
};
use hwcodec::{decode::Decoder, encode::Encoder};
use std::{
    io::{Read, Write},
    path::PathBuf,
    time::{Duration, Instant},
};

fn main() {
    let en_ctx = EncodeContext {
        driver: EncodeDriver::AMF,
        device: HWDeviceType::DX11,
        pixfmt: PixelFormat::NV12,
        dataFormat: DataFormat::H264,
        width: 2880,
        height: 1800,
    };
    let de_ctx = DecodeContext {
        driver: DecodeDriver::AMF,
        device: HWDeviceType::DX11,
        pixfmt: PixelFormat::NV12,
        dataFormat: en_ctx.dataFormat,
    };
    let mut encoder = Encoder::new(en_ctx.clone()).unwrap();
    let mut decoder = Decoder::new(de_ctx.clone()).unwrap();

    println!("pitchs:{:?}", encoder.pitchs);

    encoder.set_bitrate(5_000).unwrap();
    encoder.set_framerate(30).unwrap();

    let input_dir = PathBuf::from("D:\\tmp\\input");
    let output_dir = PathBuf::from("D:\\tmp");
    let yuv_file_name = input_dir.join("2880x1800_nv12.yuv");
    let encoded_file_name = output_dir.join(format!(
        "2880x1800_encoded.{}",
        if en_ctx.dataFormat == DataFormat::H264 {
            "264"
        } else {
            "265"
        }
    ));
    let decoded_file_name = output_dir.join("2880x1800_nv12_decoded.yuv");
    let mut yuv_file = std::fs::File::open(yuv_file_name).unwrap();
    let mut encoded_file = std::fs::File::create(encoded_file_name).unwrap();
    let mut decoded_file = std::fs::File::create(decoded_file_name).unwrap();

    let yuv_len = (en_ctx.width * en_ctx.height * 3 / 2) as usize;
    let mut yuv = vec![0u8; yuv_len];
    let mut enc_sum = Duration::ZERO;
    let mut dec_sum = Duration::ZERO;
    let mut enc_counter = 0;
    let mut dec_counter = 0;
    for _ in 0..100 {
        yuv_file.read(&mut yuv).unwrap();
        let start = Instant::now();
        let linesizes: Vec<i32> = vec![en_ctx.width, en_ctx.width];
        let ysize = (linesizes[0] * en_ctx.height) as usize;
        let y = &yuv[0..ysize];
        let uv = &yuv[ysize..];
        let datas = vec![y, uv];
        let data = encoder.encode(datas, linesizes).unwrap();
        enc_sum += start.elapsed();
        enc_counter += data.len();
        for f in data {
            encoded_file.write_all(&f.data).unwrap();
            let start = Instant::now();
            let decoded = decoder.decode(&f.data).unwrap();
            dec_sum += start.elapsed();
            dec_counter += 1;
            for f in decoded {
                for v in f.data.iter() {
                    decoded_file.write_all(v).unwrap();
                }
            }
        }
    }
    println!("enc:avg:{:?}, counter:{}", enc_sum / 100, enc_counter);
    println!("dec:avg:{:?}, counter:{}", dec_sum / 100, dec_counter);
}
