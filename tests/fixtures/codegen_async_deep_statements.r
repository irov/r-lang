module test.codegen.async_deep_statements;

/* R-LIMIT-0001, R-BORROW-0024 (L14-N1, L14-N2): an async main with its startup parameter nests
   try statements 127 levels deep, the minimum R-LIMIT-0001 requires. The startup parameter check
   walks each try body once per catch clause; it remembers what it computed, so the time it takes
   grows with the size of the body rather than doubling with each level. */
error Stop { i32 code; };

protected async i32 tick(i32 value) {
    return value + 1;
}


async i32 main(const str[] args) {
    i32 value = (len(args) as i32) - 1;
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    try {
    value += 1;
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    } catch (Stop stop) { value += stop.code; }
    if (value != 1) { return 1; }
    if (value < 1000) {
    if (value < 1001) {
    if (value < 1002) {
    if (value < 1003) {
    if (value < 1004) {
    if (value < 1005) {
    if (value < 1006) {
    if (value < 1007) {
    if (value < 1008) {
    if (value < 1009) {
    if (value < 1010) {
    if (value < 1011) {
    if (value < 1012) {
    if (value < 1013) {
    if (value < 1014) {
    if (value < 1015) {
    if (value < 1016) {
    if (value < 1017) {
    if (value < 1018) {
    if (value < 1019) {
    if (value < 1020) {
    if (value < 1021) {
    if (value < 1022) {
    if (value < 1023) {
    if (value < 1024) {
    if (value < 1025) {
    if (value < 1026) {
    if (value < 1027) {
    if (value < 1028) {
    if (value < 1029) {
    if (value < 1030) {
    if (value < 1031) {
    if (value < 1032) {
    if (value < 1033) {
    if (value < 1034) {
    if (value < 1035) {
    if (value < 1036) {
    if (value < 1037) {
    if (value < 1038) {
    if (value < 1039) {
    if (value < 1040) {
    if (value < 1041) {
    if (value < 1042) {
    if (value < 1043) {
    if (value < 1044) {
    if (value < 1045) {
    if (value < 1046) {
    if (value < 1047) {
    if (value < 1048) {
    if (value < 1049) {
    if (value < 1050) {
    if (value < 1051) {
    if (value < 1052) {
    if (value < 1053) {
    if (value < 1054) {
    if (value < 1055) {
    if (value < 1056) {
    if (value < 1057) {
    if (value < 1058) {
    if (value < 1059) {
    if (value < 1060) {
    if (value < 1061) {
    if (value < 1062) {
    if (value < 1063) {
    if (value < 1064) {
    if (value < 1065) {
    if (value < 1066) {
    if (value < 1067) {
    if (value < 1068) {
    if (value < 1069) {
    if (value < 1070) {
    if (value < 1071) {
    if (value < 1072) {
    if (value < 1073) {
    if (value < 1074) {
    if (value < 1075) {
    if (value < 1076) {
    if (value < 1077) {
    if (value < 1078) {
    if (value < 1079) {
    if (value < 1080) {
    if (value < 1081) {
    if (value < 1082) {
    if (value < 1083) {
    if (value < 1084) {
    if (value < 1085) {
    if (value < 1086) {
    if (value < 1087) {
    if (value < 1088) {
    if (value < 1089) {
    if (value < 1090) {
    if (value < 1091) {
    if (value < 1092) {
    if (value < 1093) {
    if (value < 1094) {
    if (value < 1095) {
    if (value < 1096) {
    if (value < 1097) {
    if (value < 1098) {
    if (value < 1099) {
    if (value < 1100) {
    if (value < 1101) {
    if (value < 1102) {
    if (value < 1103) {
    if (value < 1104) {
    if (value < 1105) {
    if (value < 1106) {
    if (value < 1107) {
    if (value < 1108) {
    if (value < 1109) {
    if (value < 1110) {
    if (value < 1111) {
    if (value < 1112) {
    if (value < 1113) {
    if (value < 1114) {
    if (value < 1115) {
    if (value < 1116) {
    if (value < 1117) {
    if (value < 1118) {
    if (value < 1119) {
    if (value < 1120) {
    if (value < 1121) {
    if (value < 1122) {
    if (value < 1123) {
    if (value < 1124) {
    if (value < 1125) {
    if (value < 1126) {
    value += 1;
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    }
    try {
        i32 one = await tick(0);
        value += one;
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
    if (value != 3) { return 2; }
    return 0;
}
