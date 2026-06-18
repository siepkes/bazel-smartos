licenses(["notice"])

filegroup(
    name = "srcs",
    srcs = glob(["**"]),  # glob everything to satisfy the compile.sh srcs test
    visibility = ["//third_party:__pkg__"],
)

exports_files([
    "BUILD",
    "abseil-cpp-20230125.1-illumos.patch",
])
