LockTypeInfo = provider(fields = ["lock_type"])

lock_types = [
    "mcs",
    "spin",
]

def _impl(ctx):
    lt = ctx.build_setting_value
    if lt not in lock_types:
        fail(str(ctx.label) +
             " build setting allowed to take values {" +
             ", ".join(lock_types) +
             "} but was set to unallwed value " + lt)
    return LockTypeInfo(lock_type = lt)

lock_type = rule(
    build_setting = config.string(flag = True),
    implementation = _impl,
)
