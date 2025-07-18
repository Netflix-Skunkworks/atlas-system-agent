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

    def get_spectator_cpp(self):
        thirdparty_dir = "thirdparty"
        repo = "Netflix/spectator-cpp"
        commit = "0213b0e5d8a9fce18cde5b66de2ff4adcd5034c6"
        
        zip_path = os.path.join(thirdparty_dir, f"spectator-cpp-{commit}.zip")
        dir_path = os.path.join(thirdparty_dir, "spectator-cpp")
        
        os.makedirs(thirdparty_dir, exist_ok=True)
        self.maybe_remove_file(zip_path)
        self.maybe_remove_dir(dir_path)
        
        download(self, f"https://github.com/{repo}/archive/{commit}.zip", zip_path)
        check_sha256(self, zip_path, "388a453743caca3ffaba5dadc173207a0b6977d59f7b5afe2937e2555ff521dc")
        unzip(self, zip_path, destination=dir_path, strip_root=True)
        self.maybe_remove_file(zip_path)

    def source(self):
        self.get_spectator_cpp()