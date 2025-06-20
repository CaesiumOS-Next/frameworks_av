#!/usr/bin/python3

#
# Copyright 2025, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
import argparse
import logging
import os
import re
import sys
import xml.etree.ElementTree as ET
import xml.etree.ElementInclude as EI
import xml.dom.minidom as MINIDOM

#
# AIDL Converted Output is generated as followed:
#
# ├── policy
# ├── Android.bp
# ├── engine
# ├   ├── config
# ├   ├   ├── audio_policy_engine_configuration.xml
# ├   ├   ├── audio_policy_engine_default_volumes.xml
# ├   ├   ├── audio_policy_engine_product_strategies.xml
# ├   ├   └── audio_policy_engine_volumes.xml
# ├   └── parameter-framework
# ├       └── Settings
# ├           ├── device_for_input_source.pfw
# ├           ├── device_for_product_strategies.pfw
# ├           └── volumes.pfw
#
def parseArgs():
    argparser = argparse.ArgumentParser(description="Cap Engine Legacy to AIDL configuration \
                                        files converter.\n\
                                        Exit with the number of (recoverable or not) \
                                        error that occured.")
    argparser.add_argument('--legacyEngineStrategyConfiguration',
                           help="Legacy Audio Policy Engine Configuration Strategy file, Mandatory.",
                           metavar="(LEGACY_POLICY_ENGINE_STRATEGY_CONFIGURATION_FILE)",
                           type=argparse.FileType('r'),
                           required=True)
    argparser.add_argument('--legacyPFwEdds',
                           help="List of legacy CapEngine PFW files in EDD syntax (aka \".pfw\" files)",
                           metavar="EDD_FILE",
                           type=argparse.FileType('r'),
                           nargs='*',
                           default=[],
                           dest='edd_files')
    argparser.add_argument('--androidaudiobaseheader',
                           help="Android Audio Base C header file, Mandatory.",
                           metavar="ANDROID_AUDIO_BASE_HEADER",
                           type=argparse.FileType('r'),
                           required=True)
    argparser.add_argument('--androidaudiocommonbaseheader',
                           help="Android Audio CommonBase C header file, Mandatory.",
                           metavar="ANDROID_AUDIO_COMMON_BASE_HEADER",
                           type=argparse.FileType('r'),
                           required=True)
    argparser.add_argument('--outputDir',
                           help="AIDL Generation Output dir, Mandatory.",
                           metavar="(LEGACY_POLICY_ENGINE_STRATEGY_CONFIGURATION_FILE)",
                           type=dir_path,
                           required=True)
    argparser.add_argument('--verbose',
                           action='store_true')

    return argparser.parse_args()

def dir_path(string):
    if os.path.isdir(string):
        return string
    else:
        raise NotADirectoryError(string)

def generateLegacyToAidlMappingTable(androidaudiobaseheaderFile, androidaudiocommonbaseheaderFile):
    #
    # Adaptation table between Android Enumeration prefix and Audio PFW Criterion type names
    #
    criterion_mapping_table = {
        'HAL_AUDIO_MODE' : "TelephonyMode",
        'AUDIO_DEVICE_OUT' : "AvailableOutputDevices",
        'AUDIO_DEVICE_IN' : "AvailableInputDevices"}

    all_criteria = {
        'TelephonyMode' : {},
        'AvailableOutputDevices' : {},
        'AvailableInputDevices' : {},
        'ForceUseForCommunication' : {},
        'ForceUseForMedia' : {},
        'ForceUseForRecord' : {},
        'ForceUseForDock' : {},
        'ForceUseForSystem' : {},
        'ForceUseForHdmiSystemAudio' : {},
        'ForceUseForEncodedSurround' : {},
        'ForceUseForVibrateRinging' : {},
        'ForceUseForRecord' : {},
    }

    #
    # _CNT, _MAX, _ALL and _NONE are prohibited values as ther are just helpers for enum users.
    #
    ignored_values = ['CNT', 'MAX', 'ALL', 'NONE']

    criteria_pattern = re.compile(
        r"\s*V\((?P<type>(?:"+'|'.join(criterion_mapping_table.keys()) + "))_" \
        r"(?P<literal>(?!" + '|'.join(ignored_values) + ")\w*)\s*,\s*" \
        r"(?:AUDIO_DEVICE_BIT_IN \| )?(?P<values>(?:0[xX])?[0-9a-fA-F]+|[0-9]+)")

    logging.info("Checking Android Header file {}".format(androidaudiobaseheaderFile))

    for line_number, line in enumerate(androidaudiobaseheaderFile):
        match = criteria_pattern.match(line)
        if match:
            logging.debug("The following line is VALID: {}:{}\n{}".format(
                androidaudiobaseheaderFile.name, line_number, line))

            criterion_name = criterion_mapping_table[match.groupdict()['type']]
            legacy_criterion_literal = \
                ''.join((w.capitalize() for w in match.groupdict()['literal'].split('_')))
            aidl_criterion_literal = ''.join(match.groupdict()['literal'])

            all_criteria[criterion_name][str(legacy_criterion_literal)] = str(aidl_criterion_literal)

            logging.debug("type:{},".format(criterion_name))
            logging.debug("legacy:{},".format(legacy_criterion_literal))
            logging.debug("aidl:{}.".format(aidl_criterion_literal))

    logging.info("Checking Android Common Header file {}".format(androidaudiocommonbaseheaderFile))

    criteria_pattern = re.compile(
        r"\s*(?P<type>(?:"+'|'.join(criterion_mapping_table.keys()) + "))_" \
        r"(?P<literal>(?!" + '|'.join(ignored_values) + ")\w*)\s*=\s*" \
        r"(?:AUDIO_DEVICE_BIT_IN \| )?(?P<values>(?:0[xX])?[0-9a-fA-F]+|[0-9]+)")

    for line_number, line in enumerate(androidaudiocommonbaseheaderFile):
        match = criteria_pattern.match(line)
        if match:
            logging.debug("The following line is VALID: {}:{}\n{}".format(
                androidaudiocommonbaseheaderFile.name, line_number, line))

            criterion_name = criterion_mapping_table[match.groupdict()['type']]
            legacy_criterion_literal = \
                ''.join((w.capitalize() for w in match.groupdict()['literal'].split('_')))
            aidl_criterion_literal = ''.join(match.groupdict()['literal'])

            all_criteria[criterion_name][str(legacy_criterion_literal)] = str(aidl_criterion_literal)

            logging.debug("type:{},".format(criterion_name))
            logging.debug("legacy:{},".format(legacy_criterion_literal))
            logging.debug("aidl:{}.".format(aidl_criterion_literal))

    # Todo: convert from frameworks/av/services/audiopolicy/engineconfigurable/config/example/common/audio_policy_engine_criterion_types.xml.in
    all_criteria['ForceUseForCommunication'] = { 'ForceNone' : "NONE", 'ForceSpeaker' : "SPEAKER", 'ForceBtSco' : "BT_SCO" }
    all_criteria['ForceUseForMedia'] = {'ForceNone' : "NONE", 'ForceSpeaker' : "SPEAKER", 'ForceHeadphones' : "HEADPHONES", 'ForceBtA2dp' : "BT_A2DP", 'ForceWiredAccessory' : "WIRED_ACCESSORY", 'ForceAnalogDock' : "ANALOG_DOCK",'ForceDigitalDock' : "DIGITAL_DOCK", 'ForceNoBtA2dp': "NO_BT_A2DP"}
    all_criteria['ForceUseForRecord'] = {'ForceNone' : "NONE", 'ForceBtSco' : "BT_SCO", 'ForceWiredAccessory' : "WIRED_ACCESSORY"}
    all_criteria['ForceUseForDock'] = {'ForceNone' : "NONE", 'ForceWiredAccessory' : "WIRED_ACCESSORY", 'ForceBtCarDock' : "BT_CAR_DOCK", 'ForceBtDeskDock' : "BT_DESK_DOCK", 'ForceAnalogDock' : "ANALOG_DOCK", 'ForceDigitalDock' : "DIGITAL_DOCK"}
    all_criteria['ForceUseForSystem'] = {'ForceNone' : "NONE", 'ForceSystemEnforced' : "SYSTEM_ENFORCED"}
    all_criteria['ForceUseForHdmiSystemAudio'] = {'ForceNone' : "NONE", 'ForceHdmiSystemEnforced' : "HDMI_SYSTEM_AUDIO_ENFORCED"},
    all_criteria['ForceUseForEncodedSurround'] = {'ForceNone' : "NONE", 'ForceEncodedSurroundNever' : "ENCODED_SURROUND_NEVER", 'ForceEncodedSurroundAlways' : "ENCODED_SURROUND_ALWAYS", 'ForceEncodedSurroundManual' : "ENCODED_SURROUND_MANUAL"}
    all_criteria['ForceUseForVibrateRinging'] = {'ForceNone' : "NONE", 'ForceBtSco' : "BT_SCO"}

    return all_criteria


def convertStrategyEngineConfigFile(legacyEngineStrategyConfiguration, outputDir):
    logging.info("Reading Legacy Engine Strategy Configuration file {}".format(legacyEngineStrategyConfiguration))
    legacy_strategy_config_tree = ET.parse(legacyEngineStrategyConfiguration)
    legacy_strategy_config_root = legacy_strategy_config_tree.getroot()
    vendor_strategy_id = 1000
    strategy_id_to_name_map = {}
    aidl_engine_strategy_config = "/policy/engine/config/audio_policy_engine_product_strategies.xml"
    aidl_engine_strategy_config_file = outputDir + aidl_engine_strategy_config
    if not os.path.exists(os.path.dirname(aidl_engine_strategy_config_file)):
        try:
            os.makedirs(os.path.dirname(aidl_engine_strategy_config_file))
        except OSError as exc: # Guard against race condition
            if exc.errno != errno.EEXIST:
                raise

    for strategy in legacy_strategy_config_root.findall('ProductStrategy'):
        name = strategy.get('name')
        strategy.set('id', str(vendor_strategy_id))
        strategy_id_to_name_map[name] = vendor_strategy_id
        vendor_strategy_id += 1

        if vendor_strategy_id == 1040:
            logging.error("Reaching max supported strategies by AIDL Cap Engine".format(vendor_strategy_id))
            sys.exit(main())

    with open(aidl_engine_strategy_config_file, "w") as f:
        legacy_strategy_config_tree.write(aidl_engine_strategy_config_file, encoding='UTF-8', xml_declaration=True)
    return strategy_id_to_name_map

def convertPfwEddFiles(strategy_id_to_name_map, legacy_to_aidl_criterion_map, legacyPFwEdds, outputDir):
    aidl_pfw_settings_dir = outputDir + "/policy/engine/parameter-framework/Settings/"

    for legacy_edd_file in legacyPFwEdds:
        logging.error("legacy_edd_file={}".format(legacy_edd_file))
        logging.error("basename={}".format(os.path.basename(legacy_edd_file.name)))
        aidl_edd_file = aidl_pfw_settings_dir + os.path.basename(legacy_edd_file.name)
        logging.error("aidl_edd_file={}".format(aidl_edd_file))
        if not os.path.exists(os.path.dirname(aidl_edd_file)):
            try:
                os.makedirs(os.path.dirname(aidl_edd_file))
            except OSError as exc: # Guard against race condition
                if exc.errno != errno.EEXIST:
                    raise

        criterion_rules = { 'Is', 'IsNot', 'Excludes', 'Includes'}
        criterion_rule_pattern = re.compile(
                r"\s*(?P<type>(?:"+'|'.join(legacy_to_aidl_criterion_map.keys()) + "))" \
                r"\s*(?P<rule>(?:"+'|'.join(criterion_rules) + "))" \
                r"\s*(?P<values>(\w+))")

        strategy_param_pattern = ["product_strategies", "selected_output_devices"]

        with open(aidl_edd_file, "w") as f:
            for num, line in enumerate(legacy_edd_file):
                # Line with criterion?
                match = criterion_rule_pattern.match(line)
                if match:
                    logging.debug("The following line is VALID: {}:{}".format(num, line))
                    criterion = match.groupdict()['type']
                    legacy_value = match.groupdict()['values']
                    logging.error("type={}".format(criterion))
                    logging.error("rule={}".format(match.groupdict()['rule']))
                    logging.error("values={}".format(legacy_value))

                    if str(legacy_value) in legacy_to_aidl_criterion_map[criterion].keys():
                        logging.error("valid legacy value {} mapped with aidl {}".format(legacy_value, legacy_to_aidl_criterion_map[criterion][legacy_value]))
                    else:
                        logging.error("Unsupported legacy value found {}".format(legacy_value))
                        sys.exit(main())

                    if criterion == "AvailableInputDevices":
                        # Remove ambient and in_communication since they were deprecated
                        if legacy_value == "Ambient" or legacy_value == "Communication":
                            logging.info(" deprecated device {}, fix legacy config first".format(legacy_value))
                            sys.exit(main())

                    aidl_edd_line = line.replace(legacy_value, legacy_to_aidl_criterion_map[criterion][legacy_value])
                    logging.error("aidl_edd_line={}".format(aidl_edd_line))
                    f.write(aidl_edd_line)

                elif any(pattern in line for pattern in strategy_param_pattern):
                    matching_legacy_strategy= next((pattern for pattern in strategy_id_to_name_map.keys() if pattern in line), None)
                    if matching_legacy_strategy:
                        aidl_edd_line = line.replace(matching_legacy_strategy, 'vx_' + str(strategy_id_to_name_map[matching_legacy_strategy]))
                        f.write(aidl_edd_line)
                    else:
                        f.write(line)

                elif "ambient =" in line:
                    logging.info(" deprecated ambient parameter, fix legacy config first")
                    sys.exit(main())
                elif "communication =" in line:
                    logging.info(" deprecated communication parameter, fix legacy config first")
                    sys.exit(main())
                else:
                    # copy line without changes
                    f.write(line)
            # Line with strategy parameter path?


def main():
    logging.root.setLevel(logging.INFO)
    args = parseArgs()

    strategy_id_to_name_map = convertStrategyEngineConfigFile(args.legacyEngineStrategyConfiguration, args.outputDir)
    logging.error("strategy_id_to_name_map={} ".format(strategy_id_to_name_map))

    legacy_to_aidl_criterion_map = generateLegacyToAidlMappingTable(args.androidaudiobaseheader,
                                                                    args.androidaudiocommonbaseheader)
    logging.error("legacy_to_aidl_criterion_map={} ".format(legacy_to_aidl_criterion_map))

    convertPfwEddFiles(strategy_id_to_name_map, legacy_to_aidl_criterion_map, args.edd_files, args.outputDir)

# If this file is directly executed
if __name__ == "__main__":
    sys.exit(main())