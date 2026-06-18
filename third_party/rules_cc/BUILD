licenses(["notice"])

filegroup(
    name = "srcs",
    srcs = glob(["**"]),  # glob everything to satisfy the compile.sh srcs test
    visibility = ["//third_party:__pkg__"],
)

exports_files([
    "BUILD",
    "rules_cc-0.0.11-illumos.patch",
])
