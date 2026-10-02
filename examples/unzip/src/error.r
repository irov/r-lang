module example.unzip.error;

enum ZipErrorCode {
    InvalidArguments,
    Io,
    Truncated,
    InvalidZip,
    UnsupportedZip64,
    UnsupportedEncryption,
    UnsupportedMethod,
    InvalidUtf8,
    UnsafePath,
    DuplicatePath,
    LimitExceeded,
    InvalidDeflate,
    CrcMismatch,
    Async,
    Internal
};

error ZipError {
    ZipErrorCode code;
    usize offset;
    constexpr str message;
};
