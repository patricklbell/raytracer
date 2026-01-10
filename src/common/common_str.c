internal NTString8 make_ntstr8(char* data, u64 length) {
    return (NTString8) {
        .cstr = data,
        .length = length,
    };
}

internal b8 ntstr8_begins_with(NTString8 str, const char* prefix) {
    if (strlen(prefix) > str.length) {
        return false;
    }

    for EachIndex(i, strlen(prefix)) {
        if (str.cstr[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

internal b8 ntstr8_eq(NTString8 a, NTString8 b) {
    if (a.length != b.length)
        return false;

    for (u64 i = 0; i < a.length; i++) {
        if (a.cstr[i] != b.cstr[i])
            return false;
    }
    return true;
}

internal u64 ntstr8_to_u64(NTString8 str) {
    return (u64)strtoull(str.cstr, NULL, 10);
}