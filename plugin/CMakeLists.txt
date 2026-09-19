cmake_minimum_required(VERSION 3.11)
project(PSPWave C)
add_prx_module(PSPWave main.c exports.exp)
target_link_libraries(PSPWave PRIVATE pspuser)
