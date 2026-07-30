# Returns source nodes so OTP objects land in the consumer's own library and
# inherit its flags. Mirrors the shape libcur uses, so a project already
# consuming one consumes the other the same way:
#
#     env.Append(CPPPATH=["otp/include"])
#     otp_sources = SConscript("otp/SConscript", exports="env")
#     sources = Glob("src/*.cpp") + otp_sources

Import("env")

otp_sources = [
    File("src/otp_ledger.cpp"),
]

# MSVC reads source as the local codepage unless told otherwise, and the
# citations passed through this library contain section signs.
if env.get("is_msvc", False) or env["CC"] == "cl":
    env.Append(CXXFLAGS=["/utf-8"])

Return("otp_sources")
