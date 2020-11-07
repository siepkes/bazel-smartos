# Copyright 2019 The Bazel Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""A Starlark cc_toolchain configuration rule for Illumos."""

load(
    "@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl",
    "action_config",
    "feature",
    "flag_group",
    "flag_set",
    "tool",
    "tool_path",
    "with_feature_set",
)
load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")

all_compile_actions = [
    ACTION_NAMES.c_compile,
    ACTION_NAMES.cpp_compile,
    ACTION_NAMES.linkstamp_compile,
    ACTION_NAMES.assemble,
    ACTION_NAMES.preprocess_assemble,
    ACTION_NAMES.cpp_header_parsing,
    ACTION_NAMES.cpp_module_compile,
    ACTION_NAMES.cpp_module_codegen,
    ACTION_NAMES.clif_match,
    ACTION_NAMES.lto_backend,
]

all_cpp_compile_actions = [
    ACTION_NAMES.cpp_compile,
    ACTION_NAMES.linkstamp_compile,
    ACTION_NAMES.cpp_header_parsing,
    ACTION_NAMES.cpp_module_compile,
    ACTION_NAMES.cpp_module_codegen,
    ACTION_NAMES.clif_match,
]

all_link_actions = [
    ACTION_NAMES.cpp_link_executable,
    ACTION_NAMES.cpp_link_dynamic_library,
    ACTION_NAMES.cpp_link_nodeps_dynamic_library,
]

def _impl(ctx):
    cpu = ctx.attr.cpu
    is_illumos = cpu == "illumos"
    compiler = "compiler"
    toolchain_identifier = "local_{}".format(cpu) if is_illumos else "stub_armeabi-v7a"
    host_system_name = "local" if is_illumos else "armeabi-v7a"
    target_system_name = "local" if is_illumos else "armeabi-v7a"
    target_libc = "local" if is_illumos else "armeabi-v7a"
    abi_version = "local" if is_illumos else "armeabi-v7a"
    abi_libc_version = "local" if is_illumos else "armeabi-v7a"

    objcopy_embed_data_action = action_config(
        action_name = "objcopy_embed_data",
        enabled = True,
        tools = [tool(path = "/opt/local/bin/objcopy")],
    )

    action_configs = [objcopy_embed_data_action] if is_illumos else []

    default_link_flags_feature = feature(
        name = "default_link_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = all_link_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            # gcc_s is here because it needs to be pulled in _before_ libc gets pulled in. Libraries
                            # like libxnet pull in libc so therefor we need to explicitly put libgcc_s before them.
                            # If we don't we run in to the problem that GCC's exception support (in libgcc_s) gets overriden
                            # by Illumos' libc exception support (in libc). This leads to the situation where exceptions
                            # don't work for a GCC compiled application. Applications will simply terminate when an 
                            # exception is thrown. For more info see:
                            # - https://paulbeachsblog.blogspot.com/2008/03/exceptions-gcc-and-solaris-10-amd-64bit.html
                            # - https://stackoverflow.com/questions/27490165/sun-studio-linking-gcc-libs-exceptions-do-not-work#
                            # - https://blogs.datalogics.com/2013/06/26/2013-june-dle-intel-solaris-64-mystery/
                            "-lgcc_s",
                            "-lxnet",
                            "-lsocket",
                            "-lnsl",
                            # Needed for 'proc_arg_psinfo'.
                            "-lproc",
                            "-lstdc++",
                            # Create position independent code.
                            "-fpic",
                            # Make the Illumos linker behave less strict. By default it uses '-ztext'. This caused some issues
                            # with Envoy.
                            # TODO: This doesn't belong here. Solve this properly.
                            "-Wl,-z,textoff",
                            # Remove the default '-ztext' flag which conflicts with '-ztextoff'.
                            "-mimpure-text",
                            # Make the Illumos linker rescan the archive files that are provided to the link-edit.
                            "-Wl,-z,rescan",
                        ],
                    ),
                ],
            )
        ],
    )

    unfiltered_compile_flags_feature = feature(
        name = "unfiltered_compile_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = all_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            # Identify as Illumos.
                            "-D__illumos__",                            
                            "-no-canonical-prefixes",
                            "-fno-canonical-system-headers",
                            "-Wno-builtin-macro-redefined",
                            "-D__DATE__=\"redacted\"",
                            "-D__TIMESTAMP__=\"redacted\"",
                            "-D__TIME__=\"redacted\"",
                        ],
                    ),
                ],
            ),
        ],
    )

    supports_pic_feature = feature(name = "supports_pic", enabled = True)

    default_compile_flags_feature = feature(
        name = "default_compile_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = all_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            "-U_FORTIFY_SOURCE",
                            "-D_FORTIFY_SOURCE=1",
                            # TODO: Does Solaris support this?
                            #"-fstack-protector",
                            "-Wall",
                            "-fno-omit-frame-pointer",
                        ],
                    ),
                ],
            ),
            flag_set(
                actions = all_compile_actions,
                flag_groups = [flag_group(flags = ["-g"])],
                with_features = [with_feature_set(features = ["dbg"])],
            ),
            flag_set(
                actions = all_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            "-g0",
                            "-O2",
                            "-DNDEBUG",
                            "-ffunction-sections",
                            "-fdata-sections",
                        ],
                    ),
                ],
                with_features = [with_feature_set(features = ["opt"])],
            ),
            flag_set(
                actions = all_cpp_compile_actions + [ACTION_NAMES.lto_backend],
                flag_groups = [flag_group(flags = ["-std=c++0x"])],
            ),
        ],
    )     

    opt_feature = feature(name = "opt")

    supports_dynamic_linker_feature = feature(name = "supports_dynamic_linker", enabled = True)

    objcopy_embed_flags_feature = feature(
        name = "objcopy_embed_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = ["objcopy_embed_data"],
                flag_groups = [flag_group(flags = ["-I", "binary"])],
            ),
        ],
    )

    dbg_feature = feature(name = "dbg")

    user_compile_flags_feature = feature(
        name = "user_compile_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = all_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = ["%{user_compile_flags}"],
                        iterate_over = "user_compile_flags",
                        expand_if_available = "user_compile_flags",
                    ),
                ],
            ),
        ],
    )    

    sysroot_feature = feature(
        name = "sysroot",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.c_compile,
                    ACTION_NAMES.cpp_compile,
                    ACTION_NAMES.linkstamp_compile,
                    ACTION_NAMES.preprocess_assemble,
                    ACTION_NAMES.cpp_header_parsing,
                    ACTION_NAMES.cpp_module_compile,
                    ACTION_NAMES.cpp_module_codegen,
                    ACTION_NAMES.clif_match,
                    ACTION_NAMES.lto_backend,
                ] + all_link_actions,
                flag_groups = [
                    flag_group(
                        flags = ["--sysroot=%{sysroot}"],
                        expand_if_available = "sysroot",
                    ),
                ],
            ),
        ],
    )

    if is_illumos:
        features = [
            default_compile_flags_feature,
            default_link_flags_feature,
            supports_dynamic_linker_feature,
            supports_pic_feature,
            objcopy_embed_flags_feature,
            opt_feature,
            dbg_feature,
            user_compile_flags_feature,
            sysroot_feature,
            unfiltered_compile_flags_feature,
        ]
    else:
        features = [supports_dynamic_linker_feature, supports_pic_feature]

    if (is_illumos):
        # Paths obtained with '/opt/local/gcc7/bin/g++ -E -x c++ - -v < /dev/null'.
        cxx_builtin_include_directories = ["/opt/local/gcc7/include/c++", "/opt/local/gcc7/include/c++/x86_64-sun-solaris2.11", "/opt/local/gcc7/include/c++/backward", "/opt/local/gcc7/lib/gcc/x86_64-sun-solaris2.11/7.5.0/include", "/opt/local/include", "/opt/local/gcc7/include", "/opt/local/gcc7/lib/gcc/x86_64-sun-solaris2.11/7.5.0/include-fixed", "/usr/include"]
    else:
        cxx_builtin_include_directories = []

    if is_illumos:
        tool_paths = [
            # Illumos ar doesn't have the '-D' flag which GNU ar has.
            tool_path(name = "ar", path = "/opt/local/bin/ar"),
            tool_path(name = "compat-ld", path = "/usr/bin/ld"),
            tool_path(name = "cpp", path = "/opt/local/gcc7/bin/cpp"),
            # Does not exist on Solaris.
            tool_path(name = "dwp", path = "/usr/bin/dwp"),
            tool_path(name = "gcc", path = "/opt/local/gcc7/bin/gcc"),
            tool_path(name = "gcov", path = "/opt/local/gcc7/bin/gcov"),
            tool_path(name = "ld", path = "/usr/bin/ld"),
            tool_path(name = "nm", path = "/usr/bin/nm"),
            tool_path(name = "objcopy", path = "/opt/local/bin/objcopy"),
            tool_path(name = "objdump", path = "/opt/local/bin/objdump"),
            tool_path(name = "strip", path = "/opt/local/bin/strip"),
        ]
    else:
        tool_paths = [
            tool_path(name = "ar", path = "/bin/false"),
            tool_path(name = "compat-ld", path = "/bin/false"),
            tool_path(name = "cpp", path = "/bin/false"),
            tool_path(name = "dwp", path = "/bin/false"),
            tool_path(name = "gcc", path = "/bin/false"),
            tool_path(name = "gcov", path = "/bin/false"),
            tool_path(name = "ld", path = "/bin/false"),
            tool_path(name = "nm", path = "/bin/false"),
            tool_path(name = "objcopy", path = "/bin/false"),
            tool_path(name = "objdump", path = "/bin/false"),
            tool_path(name = "strip", path = "/bin/false"),
        ]

    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(out, "Fake executable")
    return [
        cc_common.create_cc_toolchain_config_info(
            ctx = ctx,
            features = features,
            action_configs = action_configs,
            cxx_builtin_include_directories = cxx_builtin_include_directories,
            toolchain_identifier = toolchain_identifier,
            host_system_name = host_system_name,
            target_system_name = target_system_name,
            target_cpu = cpu,
            target_libc = target_libc,
            compiler = compiler,
            abi_version = abi_version,
            abi_libc_version = abi_libc_version,
            tool_paths = tool_paths,
        ),
        DefaultInfo(
            executable = out,
        ),
    ]

cc_toolchain_config = rule(
    implementation = _impl,
    attrs = {
        "cpu": attr.string(mandatory = True),
    },
    provides = [CcToolchainConfigInfo],
    executable = True,
)
