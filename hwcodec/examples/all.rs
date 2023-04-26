use duplication::dxgi;
use hw_common::{
    DataFormat, DecodeContext, DecodeDriver, DynamicContext, EncodeContext, EncodeDriver,
    FeatureContext, HWDeviceType, PixelFormat, MAX_GOP,
};
use hwcodec::{decode::Decoder, encode::Encoder};
use std::{
    io::{Read, Write},
    path::PathBuf,
    time::{Duration, Instant},
};

fn main() {
    unsafe {
        let mut dup = dxgi::Duplicator::new().unwrap();

        let en_ctx = EncodeContext {
            f: FeatureContext {
                driver: EncodeDriver::NVENC,
                device: HWDeviceType::DX11,
                pixfmt: PixelFormat::NV12,
                dataFormat: DataFormat::H264,
            },
            d: DynamicContext {
                device: dup.device(),
                width: 1920,
                height: 1080,
                kbitrate: 5000,
                framerate: 30,
                gop: MAX_GOP as _,
            },
        };
        let de_ctx = DecodeContext {
            driver: DecodeDriver::CUVID,
            deviceType: HWDeviceType::DX11,
            pixfmt: PixelFormat::NV12,
            dataFormat: DataFormat::H264,
            hdl: dup.device(),
        };

        let mut enc = Encoder::new(en_ctx).unwrap();
        let mut dec = Decoder::new(de_ctx).unwrap();
        let filename = PathBuf::from("D:\\tmp\\1.264");
        let mut file = std::fs::File::create(filename).unwrap();
        let mut dup_sum = Duration::ZERO;
        let mut enc_sum = Duration::ZERO;
        let mut dec_sum = Duration::ZERO;
        let mut counter = 0;
        for _ in 0..1000 {
            let start = Instant::now();
            let texture = dup.duplicate(100);
            if texture.is_null() {
                // println!("texture is null");
                continue;
            }
            dup_sum += start.elapsed();
            let start = Instant::now();
            if let Ok(frame) = enc.encode(texture) {
                for f in frame {
                    file.write_all(&mut f.data).unwrap();
                    // let start = Instant::now();
                    // let frames = dec.decode(&f.data).unwrap();
                    // dec_sum += start.elapsed();
                    // std::thread::sleep(Duration::from_millis(30));
                }
            }
            //     let frame = enc.encode(texture).unwrap();
            //     enc_sum += start.elapsed();
            //     counter += 1;
            //     for f in frame {
            //         file.write_all(&mut f.data).unwrap();
            //         let start = Instant::now();
            //         let frames = dec.decode(&f.data).unwrap();
            //         dec_sum += start.elapsed();
            //         std::thread::sleep(Duration::from_millis(30));
            //     }
            // }
            // println!(
            //     "cnt:{}, dup_avg:{:?}, enc_avg:{:?}, dec_avg:{:?}",
            //     counter,
            //     dup_sum / counter,
            //     enc_sum / counter,
            //     dec_sum / counter,
            // );
        }
    }
}
