load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def nflx_atlas_sysagent():
    http_archive(
        name = "spectator",
        urls = ["https://github.com/Netflix/spectator-cpp/archive/8771e291239201e385087c66df2527d9bd060800.zip"],
        strip_prefix = "spectator-cpp-8771e291239201e385087c66df2527d9bd060800",
        sha256 = "df4b63b3e5cb73bcab36c7c78a64b9501fd17a639a680a62496e8f4fba984bc3"
    )

    # GoogleTest/GoogleMock framework.
    http_archive(
        name = "com_google_googletest",
        urls = ["https://github.com/google/googletest/archive/release-1.10.0.tar.gz"],
        strip_prefix = "googletest-release-1.10.0",
        sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
    )

    http_archive(
        name = "com_google_absl",
        urls = ["https://github.com/abseil/abseil-cpp/archive/518f17501e6156f7921fbb9b68a1e420bcb10bc5.zip"],
        strip_prefix = "abseil-cpp-518f17501e6156f7921fbb9b68a1e420bcb10bc5",
        sha256 = "0baec77dcf13da93038ad6045c87e048a6cc1f5a8ad126091c804acab4a2671a",
    )

    http_archive(
        name = "com_google_tcmalloc",
        urls = ["https://github.com/google/tcmalloc/archive/b28df226c5e6dfbc488f82514244d0b800a92685.zip"],
        strip_prefix = "tcmalloc-b28df226c5e6dfbc488f82514244d0b800a92685",
        sha256 = "8568fb0aaf3961bf26cd3ae570580e9ed1ce419c607b80b497f4a54f2c7441fa",
    )

    http_archive(
        name = "com_github_tencent_rapidjson",
        build_file = "@nflx_atlas_sysagent//third_party:rapidjson.BUILD",
        sha256 = "bf7ced29704a1e696fbccf2a2b4ea068e7774fa37f6d7dd4039d0787f8bed98e",
        strip_prefix = "rapidjson-1.1.0",
        urls = ["https://github.com/Tencent/rapidjson/archive/v1.1.0.tar.gz"],
    )

    # C++ rules for Bazel.
    http_archive(
        name = "rules_cc",
        urls = ["https://github.com/bazelbuild/rules_cc/archive/02becfef8bc97bda4f9bb64e153f1b0671aec4ba.zip"],
        strip_prefix = "rules_cc-02becfef8bc97bda4f9bb64e153f1b0671aec4ba",
        sha256 = "fa42eade3cad9190c2a6286a6213f07f1a83d26d9f082d56f526d014c6ea7444",
    )

    http_archive(
        name = "curl",
        build_file = "@nflx_atlas_sysagent//third_party:curl.BUILD",
        strip_prefix = "curl-7.72.0",
        sha256 = "d4d5899a3868fbb6ae1856c3e55a32ce35913de3956d1973caccd37bd0174fa2",
        urls = ["https://curl.haxx.se/download/curl-7.72.0.tar.gz"],
    )

    http_archive(
        name = "com_github_c_ares_c_ares",
        build_file = "@nflx_atlas_sysagent//third_party/cares:cares.BUILD",
        strip_prefix = "c-ares-1.15.0",
        sha256 = "6cdb97871f2930530c97deb7cf5c8fa4be5a0b02c7cea6e7c7667672a39d6852",
        url = "https://github.com/c-ares/c-ares/releases/download/cares-1_15_0/c-ares-1.15.0.tar.gz",
    )

    http_archive(
        name = "boringssl",
        sha256 = "1188e29000013ed6517168600fc35a010d58c5d321846d6a6dfee74e4c788b45",
        strip_prefix = "boringssl-7f634429a04abc48e2eb041c81c5235816c96514",
        urls = ["https://github.com/google/boringssl/archive/7f634429a04abc48e2eb041c81c5235816c96514.tar.gz"],
    )

    http_archive(
        name = "net_zlib",
        build_file = "@nflx_atlas_sysagent//third_party:zlib.BUILD",
        sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
        strip_prefix = "zlib-1.2.11",
        urls = [
            "https://mirror.bazel.build/zlib.net/zlib-1.2.11.tar.gz",
            "https://zlib.net/zlib-1.2.11.tar.gz",
        ],
    )

    http_archive(
        name = "com_github_bombela_backward",
        urls = ["https://github.com/bombela/backward-cpp/archive/7cf6cc9be1c50ecb44bb41e1024439a2d0629f93.zip"],
        strip_prefix = "backward-cpp-7cf6cc9be1c50ecb44bb41e1024439a2d0629f93",
        build_file = "@nflx_atlas_sysagent//third_party:backward.BUILD",
        sha256 = "bf407d60e06183e899bc009dd3b6736b7d7ff47256371d7980eebfda89f11efa",
    )
