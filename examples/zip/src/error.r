module example.zip.error;

enum ZipErrorCode {
    InvalidArguments,
    Io,
    UnsafePath,
    DuplicatePath,
    LimitExceeded,
    Allocation,
    Async,
    Internal
};

error ZipError {
    ZipErrorCode code;
    usize offset;
    constexpr str message;
};
