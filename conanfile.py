import os
import shutil

from conan import ConanFile
from conan.tools.files import download, unzip, check_sha256, load, save


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
        # TODO: remove this when SystemD updates package for xz_utils
        self.requires("xz_utils/5.8.1", override=True)

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
        commit = "1e69c442528a8450ab48948fef964fc79a73ac49"

        zip_path = os.path.join(thirdparty_dir, f"spectator-cpp-{commit}.zip")
        dir_path = os.path.join(thirdparty_dir, "spectator-cpp")

        os.makedirs(thirdparty_dir, exist_ok=True)
        self.maybe_remove_file(zip_path)
        self.maybe_remove_dir(dir_path)

        download(self, f"https://github.com/{repo}/archive/{commit}.zip", zip_path)
        check_sha256(self, zip_path, "d39cbc2f101c5ae04324d2255ca8f208985f2879a2289ab74e0207fca79d76ce")
        unzip(self, zip_path, destination=dir_path, strip_root=True)
        self.maybe_remove_file(zip_path)

    def get_rocm_systems(self):
        thirdparty_dir = "thirdparty"
        repo = "ROCm/rocm-systems"
        tag = "rocm-7.2.3"

        zip_path = os.path.join(thirdparty_dir, f"rocm-systems-{tag}.zip")
        dir_path = os.path.join(thirdparty_dir, "rocm-systems")

        os.makedirs(thirdparty_dir, exist_ok=True)
        self.maybe_remove_file(zip_path)
        self.maybe_remove_dir(dir_path)

        download(self, f"https://github.com/{repo}/archive/refs/tags/{tag}.zip", zip_path)
        check_sha256(self, zip_path, "8874d65b072e0f915b4f334bec7d61fd09ecb3cbd066b58a06afe15365e46338")
        unzip(self, zip_path, destination=dir_path, strip_root=True)
        self.disable_goamdsmi_shim(dir_path)
        self.maybe_remove_file(zip_path)

    def disable_goamdsmi_shim(self, rocm_dir: str):
        # AMD SMI unconditionally builds its Go shim (goamdsmi_shim -> libgoamdsmi_shim64.so) via a
        # bare add_subdirectory() with no gating option. The shim hardcodes the x86-only flags
        # -m64/-msse/-msse2 with no architecture guard, so on aarch64 it fails to link
        # ("unrecognized command-line option '-m64'") and breaks the whole build. We only ever link
        # libamd_smi.a (whose own x86 flags ARE arch-guarded, so it builds on every arch), so
        # comment out the line that adds the shim.
        cmakelists = os.path.join(rocm_dir, "projects", "amdsmi", "CMakeLists.txt")
        marker = "add_subdirectory(goamdsmi_shim)"
        note = "disabled by atlas-system-agent: unused Go shim, x86-only flags break aarch64"

        contents = load(self, cmakelists)
        if note in contents:
            return  # already patched

        lines = contents.splitlines(keepends=True)
        for i, line in enumerate(lines):
            if line.strip() == marker:
                newline = "\n" if line.endswith("\n") else ""
                lines[i] = "# {}  # {}{}".format(marker, note, newline)
                save(self, cmakelists, "".join(lines))
                self.output.info("Disabled goamdsmi_shim build (aarch64 compatibility)")
                return

        self.output.warning(
            "Could not find '{}' in {}; amdsmi layout may have changed".format(marker, cmakelists))

    def source(self):
        self.get_spectator_cpp()
        self.get_rocm_systems()