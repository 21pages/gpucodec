use hw_common::{
    DataFormat, DecodeContext, DecodeDriver, DynamicContext, EncodeContext, EncodeDriver,
    FeatureContext, HWDeviceType, PixelFormat, MAX_GOP,
};
use hwcodec::{decode::Decoder, encode::Encoder};
use std::{io::Write, path::PathBuf};

fn main() {
    let width = 2880;
    let height = 1800;
    let linesize_y = 2880;
    let linesize_uv = 2880;
    let max = 1;

    let en_ctx = EncodeContext {
        f: FeatureContext {
            driver: EncodeDriver::AMF,
            api: HWDeviceType::DX11,
            pixfmt: PixelFormat::NV12,
            dataFormat: DataFormat::H264,
        },
        d: DynamicContext {
            width: 2880,
            height: 1800,
            kbitrate: 5000,
            framerate: 30,
            gop: MAX_GOP as _,
        },
    };
    let de_ctx = DecodeContext {
        driver: DecodeDriver::AMF,
        device: HWDeviceType::DX11,
        pixfmt: PixelFormat::NV12,
        dataFormat: en_ctx.f.dataFormat,
    };
    let mut encoder = Encoder::new(en_ctx.clone()).unwrap();
    let mut decoder = Decoder::new(de_ctx.clone()).unwrap();

    encoder.set_bitrate(5_000).unwrap();
    encoder.set_framerate(30).unwrap();

    let output_dir = PathBuf::from("D:\\tmp");
    let encoded_file_name = output_dir.join(format!(
        "2880x1800_encoded.{}",
        if en_ctx.f.dataFormat == DataFormat::H264 {
            "264"
        } else {
            "265"
        }
    ));
    let decoded_file_name = output_dir.join("2880x1800_nv12_decoded.yuv");
    let mut encoded_file = std::fs::File::create(encoded_file_name).unwrap();
    let mut decoded_file = std::fs::File::create(decoded_file_name).unwrap();

    let yuv = generate_yuv(width, height, linesize_y, linesize_uv);
    for _ in 0..max {
        let linesizes: Vec<i32> = vec![linesize_y, linesize_uv];
        let data = vec![yuv[0].as_slice(), &yuv[1].as_slice()];
        let data = encoder.encode(data, linesizes).unwrap();

        for f in data {
            encoded_file.write_all(&f.data).unwrap();
            let decoded = decoder.decode(&f.data).unwrap();

            for f in decoded {
                for v in f.data.iter() {
                    decoded_file.write_all(v).unwrap();
                }
            }
        }
    }
    println!("finish");
}

fn generate_yuv(_w: i32, h: i32, linesize_y: i32, linesize_uv: i32) -> Vec<Vec<u8>> {
    let len_y = h * linesize_y;
    let len_uv = h * linesize_uv / 2;
    let y = vec![0u8; len_y as usize];
    let uv = vec![0u8; len_uv as usize];
    vec![y, uv]
}
