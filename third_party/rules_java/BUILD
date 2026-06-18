licenses(["notice"])

filegroup(
    name = "srcs",
    srcs = glob(["**"]),  # glob everything to satisfy the compile.sh srcs test
    visibility = ["//third_party:__pkg__"],
)

exports_files([
    "BUILD",
    "rules_java-7.6.5-illumos.patch",
])
