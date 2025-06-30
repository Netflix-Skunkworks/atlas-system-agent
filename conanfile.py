import os
import shutil

from conan import ConanFile
from conan.tools.files import download, unzip, check_sha256


class AtlasSystemAgentConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    requires = (
        "abseil/20240116.2",
        "asio/1.32.0",
        "backward-cpp/1.6",
        "boost/1.83.0",
        "fmt/11.0.2",
        "gtest/1.15.0",
        "libcurl/8.10.1",
        "openssl/3.3.2",
        "rapidjson/cci.20230929",
        "sdbus-cpp/2.0.0",
        "spdlog/1.15.0",
        "zlib/1.3.1",
    )
    tool_requires = ()
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        # TODO: remove this when SystemD updates package for zstd
        self.requires("zstd/1.5.7", override=True)

    def configure(self):
        self.options["libcurl"].with_c_ares = True
        self.options["libcurl"].with_ssl = "openssl"

    @staticmethod
    def maybe_remove_dir(path: str):
        if os.path.isdir(path):
            shutil.rmtree(path)

    @staticmethod
    def maybe_remove_file(path: str):
        if os.path.isfile(path):
            os.unlink(path)

