module test.codegen.deep_statements;

/* R-LIMIT-0001, R-LIMIT-0003 (L14-N1): every phase after the parser accepts the nesting the
   parser accepts. Each function nests one statement form 127 levels deep, the minimum R-LIMIT-0001
   requires; walks of the AST and HIR trees admit four times the source nesting. */
error Stop { i32 code; };


i32 deep_if(i32 seed) {
    i32 value = seed;
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
    return value;
}

i32 deep_while(i32 seed) {
    i32 value = seed;
    while (value < 1000) {
    while (value < 1001) {
    while (value < 1002) {
    while (value < 1003) {
    while (value < 1004) {
    while (value < 1005) {
    while (value < 1006) {
    while (value < 1007) {
    while (value < 1008) {
    while (value < 1009) {
    while (value < 1010) {
    while (value < 1011) {
    while (value < 1012) {
    while (value < 1013) {
    while (value < 1014) {
    while (value < 1015) {
    while (value < 1016) {
    while (value < 1017) {
    while (value < 1018) {
    while (value < 1019) {
    while (value < 1020) {
    while (value < 1021) {
    while (value < 1022) {
    while (value < 1023) {
    while (value < 1024) {
    while (value < 1025) {
    while (value < 1026) {
    while (value < 1027) {
    while (value < 1028) {
    while (value < 1029) {
    while (value < 1030) {
    while (value < 1031) {
    while (value < 1032) {
    while (value < 1033) {
    while (value < 1034) {
    while (value < 1035) {
    while (value < 1036) {
    while (value < 1037) {
    while (value < 1038) {
    while (value < 1039) {
    while (value < 1040) {
    while (value < 1041) {
    while (value < 1042) {
    while (value < 1043) {
    while (value < 1044) {
    while (value < 1045) {
    while (value < 1046) {
    while (value < 1047) {
    while (value < 1048) {
    while (value < 1049) {
    while (value < 1050) {
    while (value < 1051) {
    while (value < 1052) {
    while (value < 1053) {
    while (value < 1054) {
    while (value < 1055) {
    while (value < 1056) {
    while (value < 1057) {
    while (value < 1058) {
    while (value < 1059) {
    while (value < 1060) {
    while (value < 1061) {
    while (value < 1062) {
    while (value < 1063) {
    while (value < 1064) {
    while (value < 1065) {
    while (value < 1066) {
    while (value < 1067) {
    while (value < 1068) {
    while (value < 1069) {
    while (value < 1070) {
    while (value < 1071) {
    while (value < 1072) {
    while (value < 1073) {
    while (value < 1074) {
    while (value < 1075) {
    while (value < 1076) {
    while (value < 1077) {
    while (value < 1078) {
    while (value < 1079) {
    while (value < 1080) {
    while (value < 1081) {
    while (value < 1082) {
    while (value < 1083) {
    while (value < 1084) {
    while (value < 1085) {
    while (value < 1086) {
    while (value < 1087) {
    while (value < 1088) {
    while (value < 1089) {
    while (value < 1090) {
    while (value < 1091) {
    while (value < 1092) {
    while (value < 1093) {
    while (value < 1094) {
    while (value < 1095) {
    while (value < 1096) {
    while (value < 1097) {
    while (value < 1098) {
    while (value < 1099) {
    while (value < 1100) {
    while (value < 1101) {
    while (value < 1102) {
    while (value < 1103) {
    while (value < 1104) {
    while (value < 1105) {
    while (value < 1106) {
    while (value < 1107) {
    while (value < 1108) {
    while (value < 1109) {
    while (value < 1110) {
    while (value < 1111) {
    while (value < 1112) {
    while (value < 1113) {
    while (value < 1114) {
    while (value < 1115) {
    while (value < 1116) {
    while (value < 1117) {
    while (value < 1118) {
    while (value < 1119) {
    while (value < 1120) {
    while (value < 1121) {
    while (value < 1122) {
    while (value < 1123) {
    while (value < 1124) {
    while (value < 1125) {
    while (value < 1126) {
    value += 1;
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    break; }
    return value;
}

i32 deep_for(i32 seed) {
    i32 value = seed;
    for (i32 k0 = 0; k0 < 1; k0 += 1) {
    for (i32 k1 = 0; k1 < 1; k1 += 1) {
    for (i32 k2 = 0; k2 < 1; k2 += 1) {
    for (i32 k3 = 0; k3 < 1; k3 += 1) {
    for (i32 k4 = 0; k4 < 1; k4 += 1) {
    for (i32 k5 = 0; k5 < 1; k5 += 1) {
    for (i32 k6 = 0; k6 < 1; k6 += 1) {
    for (i32 k7 = 0; k7 < 1; k7 += 1) {
    for (i32 k8 = 0; k8 < 1; k8 += 1) {
    for (i32 k9 = 0; k9 < 1; k9 += 1) {
    for (i32 k10 = 0; k10 < 1; k10 += 1) {
    for (i32 k11 = 0; k11 < 1; k11 += 1) {
    for (i32 k12 = 0; k12 < 1; k12 += 1) {
    for (i32 k13 = 0; k13 < 1; k13 += 1) {
    for (i32 k14 = 0; k14 < 1; k14 += 1) {
    for (i32 k15 = 0; k15 < 1; k15 += 1) {
    for (i32 k16 = 0; k16 < 1; k16 += 1) {
    for (i32 k17 = 0; k17 < 1; k17 += 1) {
    for (i32 k18 = 0; k18 < 1; k18 += 1) {
    for (i32 k19 = 0; k19 < 1; k19 += 1) {
    for (i32 k20 = 0; k20 < 1; k20 += 1) {
    for (i32 k21 = 0; k21 < 1; k21 += 1) {
    for (i32 k22 = 0; k22 < 1; k22 += 1) {
    for (i32 k23 = 0; k23 < 1; k23 += 1) {
    for (i32 k24 = 0; k24 < 1; k24 += 1) {
    for (i32 k25 = 0; k25 < 1; k25 += 1) {
    for (i32 k26 = 0; k26 < 1; k26 += 1) {
    for (i32 k27 = 0; k27 < 1; k27 += 1) {
    for (i32 k28 = 0; k28 < 1; k28 += 1) {
    for (i32 k29 = 0; k29 < 1; k29 += 1) {
    for (i32 k30 = 0; k30 < 1; k30 += 1) {
    for (i32 k31 = 0; k31 < 1; k31 += 1) {
    for (i32 k32 = 0; k32 < 1; k32 += 1) {
    for (i32 k33 = 0; k33 < 1; k33 += 1) {
    for (i32 k34 = 0; k34 < 1; k34 += 1) {
    for (i32 k35 = 0; k35 < 1; k35 += 1) {
    for (i32 k36 = 0; k36 < 1; k36 += 1) {
    for (i32 k37 = 0; k37 < 1; k37 += 1) {
    for (i32 k38 = 0; k38 < 1; k38 += 1) {
    for (i32 k39 = 0; k39 < 1; k39 += 1) {
    for (i32 k40 = 0; k40 < 1; k40 += 1) {
    for (i32 k41 = 0; k41 < 1; k41 += 1) {
    for (i32 k42 = 0; k42 < 1; k42 += 1) {
    for (i32 k43 = 0; k43 < 1; k43 += 1) {
    for (i32 k44 = 0; k44 < 1; k44 += 1) {
    for (i32 k45 = 0; k45 < 1; k45 += 1) {
    for (i32 k46 = 0; k46 < 1; k46 += 1) {
    for (i32 k47 = 0; k47 < 1; k47 += 1) {
    for (i32 k48 = 0; k48 < 1; k48 += 1) {
    for (i32 k49 = 0; k49 < 1; k49 += 1) {
    for (i32 k50 = 0; k50 < 1; k50 += 1) {
    for (i32 k51 = 0; k51 < 1; k51 += 1) {
    for (i32 k52 = 0; k52 < 1; k52 += 1) {
    for (i32 k53 = 0; k53 < 1; k53 += 1) {
    for (i32 k54 = 0; k54 < 1; k54 += 1) {
    for (i32 k55 = 0; k55 < 1; k55 += 1) {
    for (i32 k56 = 0; k56 < 1; k56 += 1) {
    for (i32 k57 = 0; k57 < 1; k57 += 1) {
    for (i32 k58 = 0; k58 < 1; k58 += 1) {
    for (i32 k59 = 0; k59 < 1; k59 += 1) {
    for (i32 k60 = 0; k60 < 1; k60 += 1) {
    for (i32 k61 = 0; k61 < 1; k61 += 1) {
    for (i32 k62 = 0; k62 < 1; k62 += 1) {
    for (i32 k63 = 0; k63 < 1; k63 += 1) {
    for (i32 k64 = 0; k64 < 1; k64 += 1) {
    for (i32 k65 = 0; k65 < 1; k65 += 1) {
    for (i32 k66 = 0; k66 < 1; k66 += 1) {
    for (i32 k67 = 0; k67 < 1; k67 += 1) {
    for (i32 k68 = 0; k68 < 1; k68 += 1) {
    for (i32 k69 = 0; k69 < 1; k69 += 1) {
    for (i32 k70 = 0; k70 < 1; k70 += 1) {
    for (i32 k71 = 0; k71 < 1; k71 += 1) {
    for (i32 k72 = 0; k72 < 1; k72 += 1) {
    for (i32 k73 = 0; k73 < 1; k73 += 1) {
    for (i32 k74 = 0; k74 < 1; k74 += 1) {
    for (i32 k75 = 0; k75 < 1; k75 += 1) {
    for (i32 k76 = 0; k76 < 1; k76 += 1) {
    for (i32 k77 = 0; k77 < 1; k77 += 1) {
    for (i32 k78 = 0; k78 < 1; k78 += 1) {
    for (i32 k79 = 0; k79 < 1; k79 += 1) {
    for (i32 k80 = 0; k80 < 1; k80 += 1) {
    for (i32 k81 = 0; k81 < 1; k81 += 1) {
    for (i32 k82 = 0; k82 < 1; k82 += 1) {
    for (i32 k83 = 0; k83 < 1; k83 += 1) {
    for (i32 k84 = 0; k84 < 1; k84 += 1) {
    for (i32 k85 = 0; k85 < 1; k85 += 1) {
    for (i32 k86 = 0; k86 < 1; k86 += 1) {
    for (i32 k87 = 0; k87 < 1; k87 += 1) {
    for (i32 k88 = 0; k88 < 1; k88 += 1) {
    for (i32 k89 = 0; k89 < 1; k89 += 1) {
    for (i32 k90 = 0; k90 < 1; k90 += 1) {
    for (i32 k91 = 0; k91 < 1; k91 += 1) {
    for (i32 k92 = 0; k92 < 1; k92 += 1) {
    for (i32 k93 = 0; k93 < 1; k93 += 1) {
    for (i32 k94 = 0; k94 < 1; k94 += 1) {
    for (i32 k95 = 0; k95 < 1; k95 += 1) {
    for (i32 k96 = 0; k96 < 1; k96 += 1) {
    for (i32 k97 = 0; k97 < 1; k97 += 1) {
    for (i32 k98 = 0; k98 < 1; k98 += 1) {
    for (i32 k99 = 0; k99 < 1; k99 += 1) {
    for (i32 k100 = 0; k100 < 1; k100 += 1) {
    for (i32 k101 = 0; k101 < 1; k101 += 1) {
    for (i32 k102 = 0; k102 < 1; k102 += 1) {
    for (i32 k103 = 0; k103 < 1; k103 += 1) {
    for (i32 k104 = 0; k104 < 1; k104 += 1) {
    for (i32 k105 = 0; k105 < 1; k105 += 1) {
    for (i32 k106 = 0; k106 < 1; k106 += 1) {
    for (i32 k107 = 0; k107 < 1; k107 += 1) {
    for (i32 k108 = 0; k108 < 1; k108 += 1) {
    for (i32 k109 = 0; k109 < 1; k109 += 1) {
    for (i32 k110 = 0; k110 < 1; k110 += 1) {
    for (i32 k111 = 0; k111 < 1; k111 += 1) {
    for (i32 k112 = 0; k112 < 1; k112 += 1) {
    for (i32 k113 = 0; k113 < 1; k113 += 1) {
    for (i32 k114 = 0; k114 < 1; k114 += 1) {
    for (i32 k115 = 0; k115 < 1; k115 += 1) {
    for (i32 k116 = 0; k116 < 1; k116 += 1) {
    for (i32 k117 = 0; k117 < 1; k117 += 1) {
    for (i32 k118 = 0; k118 < 1; k118 += 1) {
    for (i32 k119 = 0; k119 < 1; k119 += 1) {
    for (i32 k120 = 0; k120 < 1; k120 += 1) {
    for (i32 k121 = 0; k121 < 1; k121 += 1) {
    for (i32 k122 = 0; k122 < 1; k122 += 1) {
    for (i32 k123 = 0; k123 < 1; k123 += 1) {
    for (i32 k124 = 0; k124 < 1; k124 += 1) {
    for (i32 k125 = 0; k125 < 1; k125 += 1) {
    for (i32 k126 = 0; k126 < 1; k126 += 1) {
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
    return value;
}

i32 deep_block(i32 seed) {
    i32 value = seed;
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
    {
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
    return value;
}

i32 deep_try(i32 seed) {
    i32 value = seed;
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
    return value;
}

i32 deep_switch(i32 seed) {
    i32 value = seed;
    switch (value) { case 5000: value += 0; break; default: {
    switch (value) { case 5001: value += 0; break; default: {
    switch (value) { case 5002: value += 0; break; default: {
    switch (value) { case 5003: value += 0; break; default: {
    switch (value) { case 5004: value += 0; break; default: {
    switch (value) { case 5005: value += 0; break; default: {
    switch (value) { case 5006: value += 0; break; default: {
    switch (value) { case 5007: value += 0; break; default: {
    switch (value) { case 5008: value += 0; break; default: {
    switch (value) { case 5009: value += 0; break; default: {
    switch (value) { case 5010: value += 0; break; default: {
    switch (value) { case 5011: value += 0; break; default: {
    switch (value) { case 5012: value += 0; break; default: {
    switch (value) { case 5013: value += 0; break; default: {
    switch (value) { case 5014: value += 0; break; default: {
    switch (value) { case 5015: value += 0; break; default: {
    switch (value) { case 5016: value += 0; break; default: {
    switch (value) { case 5017: value += 0; break; default: {
    switch (value) { case 5018: value += 0; break; default: {
    switch (value) { case 5019: value += 0; break; default: {
    switch (value) { case 5020: value += 0; break; default: {
    switch (value) { case 5021: value += 0; break; default: {
    switch (value) { case 5022: value += 0; break; default: {
    switch (value) { case 5023: value += 0; break; default: {
    switch (value) { case 5024: value += 0; break; default: {
    switch (value) { case 5025: value += 0; break; default: {
    switch (value) { case 5026: value += 0; break; default: {
    switch (value) { case 5027: value += 0; break; default: {
    switch (value) { case 5028: value += 0; break; default: {
    switch (value) { case 5029: value += 0; break; default: {
    switch (value) { case 5030: value += 0; break; default: {
    switch (value) { case 5031: value += 0; break; default: {
    switch (value) { case 5032: value += 0; break; default: {
    switch (value) { case 5033: value += 0; break; default: {
    switch (value) { case 5034: value += 0; break; default: {
    switch (value) { case 5035: value += 0; break; default: {
    switch (value) { case 5036: value += 0; break; default: {
    switch (value) { case 5037: value += 0; break; default: {
    switch (value) { case 5038: value += 0; break; default: {
    switch (value) { case 5039: value += 0; break; default: {
    switch (value) { case 5040: value += 0; break; default: {
    switch (value) { case 5041: value += 0; break; default: {
    switch (value) { case 5042: value += 0; break; default: {
    switch (value) { case 5043: value += 0; break; default: {
    switch (value) { case 5044: value += 0; break; default: {
    switch (value) { case 5045: value += 0; break; default: {
    switch (value) { case 5046: value += 0; break; default: {
    switch (value) { case 5047: value += 0; break; default: {
    switch (value) { case 5048: value += 0; break; default: {
    switch (value) { case 5049: value += 0; break; default: {
    switch (value) { case 5050: value += 0; break; default: {
    switch (value) { case 5051: value += 0; break; default: {
    switch (value) { case 5052: value += 0; break; default: {
    switch (value) { case 5053: value += 0; break; default: {
    switch (value) { case 5054: value += 0; break; default: {
    switch (value) { case 5055: value += 0; break; default: {
    switch (value) { case 5056: value += 0; break; default: {
    switch (value) { case 5057: value += 0; break; default: {
    switch (value) { case 5058: value += 0; break; default: {
    switch (value) { case 5059: value += 0; break; default: {
    switch (value) { case 5060: value += 0; break; default: {
    switch (value) { case 5061: value += 0; break; default: {
    switch (value) { case 5062: value += 0; break; default: {
    value += 1;
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    } break; }
    return value;
}

i32 inc(i32 value) {
    return value + 1;
}

i32 deep_calls(i32 seed) {
    return inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(inc(seed))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))));
}

i32 deep_parens(i32 seed) {
    i32 value = seed;
    value += ((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((value + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1) + 1);
    return value;
}

i32 main(const str[] args) {
    i32 seed = (len(args) as i32) - 1;
    if (deep_if(seed) != 1) { return 1; }
    if (deep_while(seed) != 1) { return 2; }
    if (deep_for(seed) != 1) { return 3; }
    if (deep_block(seed) != 1) { return 4; }
    if (deep_try(seed) != 1) { return 5; }
    if (deep_switch(seed) != 1) { return 6; }
    if (deep_calls(seed) != 100) { return 7; }
    if (deep_parens(seed) != 100) { return 8; }
    return 0;
}
