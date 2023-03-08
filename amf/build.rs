use cc::Build;
use std::{
    env,
    path::{Path, PathBuf},
};

fn main() {
    let mut builder = Build::new();

    builder.include(format!("AMF/amf/public/common"));
    for f in vec![
        "AMFFactory.cpp",
        "AMFSTL.cpp",
        "DataStreamFactory.cpp",
        "DataStreamFile.cpp",
        "DataStreamMemory.cpp",
        "Thread.cpp",
        "Windows/ThreadWindows.cpp",
        "TraceAdapter.cpp",
    ] {
        builder.file(format!("AMF/amf/public/common/{}", f));
    }

    builder.include(format!("AMF/amf/public/samples/CPPSamples/common"));
    for f in vec![
        "CmdLineParser.cpp",
        "CmdLogger.cpp",
        "EncoderParamsAV1.cpp",
        "EncoderParamsAVC.cpp",
        "EncoderParamsHEVC.cpp",
        "ParametersStorage.cpp",
        "RawStreamReader.cpp",
    ] {
        builder.file(format!("AMF/amf/public/samples/CPPSamples/common/{}", f));
    }

    builder.include(format!("AMF/amf/public/samples/CPPSamples/EncoderLatency"));
    builder.file(format!(
        "AMF/amf/public/samples/CPPSamples/EncoderLatency/SurfaceGenerator.cpp"
    ));

    builder
        .includes(Some(PathBuf::from("AMF/amf")))
        .file("src/encode.cpp")
        .cpp(false)
        .compile("amf");
}
