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

def parseArgs():
    argparser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter,
        description="""Cap Engine Legacy to AIDL configuration files converter.
        Exit with the number of (recoverable or not) error that occurred.

        This tool provides standalone utility to make the transition form legacy HIDL CapEngine
        configuration to AIDL CapEngine

        Invoke this script:
          python3 legacyToAidlCapEngineConfigConversion.py
            --legacyEngineStrategyConfiguration  <path_to_HIDL_strategies>/audio_policy_engine_product_strategies.xml
            --legacyPFwEdds  <path_to_HIDL_pfw>/parameter-framework/Settings/device_for_product_strategies.pfw
                             <path_to_HIDL_pfw>/parameter-framework/Settings/device_for_input_source.pfw
                             <path_to_HIDL_pfw>//parameter-framework/Settings/volumes.pfw
            --androidTreeRoot <ANDROID ROOT TREE>
            --outputDir <path_to_store_AIDL_configuration>

        Legacy to AIDL
        The following files remains in vendor partition and are consumed by AHAL (delivered as an example)

        /vendor/etc:
            audio_policy_engine_configuration.xml [UNCHANGED]
            audio_policy_engine_criteria.xml [UNCHANGED]
            audio_policy_engine_criterion_types.xml [CHANGED]
                 [PREVIOUSLY GENERATED at buildtime by buildPolicyCriterionType.py]
                 [GENERATED at buildtime by capBuildPolicyCriterionType.py]
            audio_policy_engine_default_volumes.xml [UNCHANGED]
            audio_policy_engine_product_strategies.xml [UPDATED by legacyToAidlCapEngineConfigConversion]
            audio_policy_engine_volumes.xml [UNCHANGED]

        Notes:
         About audio_policy_engine_criterion_types.xml
           as build time tools are provided, this script does not update this configuration file.
         About audio_policy_engine_product_strategies.xml
           this script will assign a strategy id in the order of the current file.
           -For legacy strategies, it uses the AIDL id found in
            <ANDROID_ROOT>system/hardware/interfaces/media/aidl/android/media/audio/common/AudioProductStrategyType.aidl
           -For vendor strategies, it assignes an id starting from VENDOR_STRATEGY_ID_START (1000)
            until 1039 since 40 strategies are provisionned by AOSP.
           The Id will be used as the priority of the strategy, the lowest the highest priority.

        /vendor/etc/parameter-framework
        ├── ParameterFrameworkConfigurationPolicy.xml [Moved to System partition, not handled by OEM]
        ├── /Settings
        │  └── /Policy
        │     └── PolicyConfigurableDomains.xml     [UPDATED by legacyToAidlCapEngineConfigConversion]
        └── /Structure
            └── /Policy
                ├── PolicyClass.xml                 [Moved to System partition, not handled by OEM]
                ├── PolicySubsystem-CommonTypes.xml [Moved to System partition, not handled by OEM]
                ├── PolicySubsystem.xml             [Moved to System partition, not handled by OEM]
                └── ProductStrategies.xml           [Moved to System partition, not handled by OEM]

        Notes:
          About the PolicyConfigurableDomains: it is generated at build time from pfw file
            legacyToAidlCapEngineConfigConversion will translate only pfw files
            translation into XML will still be managed at build time.
          About device_for_product_strategies.pfw
            Strategies are now referred by their id
              -Vendor Strategies parameter path are automatically replaced with the vx_<id> assigned by the script
               when converting the engine configuration file.
              -Legacy Strategies parameter remains with the literal STRATEGY_<LEGACY_NAME>
          About device_for_input_sources.pfw
            Communication and Ambient are deprecated in HIDL but not supported in AIDL.
            Any occurrence as criterion type value or parameter will be skipped.
            It ia however advise to clean these value manually first.
          About all pfw
            Criterion type values are migrated from upper camel case to capital letters using
              underscore as delimiter.

          Output is generated as follows:

         ├── /policy
         ├── /engine
         ├   ├── config
         ├   ├   ├── audio_policy_engine_product_strategies.xml
         ├   └── /parameter-framework
         ├       └── /Settings
         ├           ├── device_for_input_source.pfw
         ├           ├── device_for_product_strategies.pfw
         ├           └── volumes.pfw

        """)
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
    argparser.add_argument('--androidTreeRoot',
                           help="Android Tree Root, Mandatory.",
                           metavar="ANDROID_ROOT_TREE",
                           type=dir_path,
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

def generateLegacyToAidlMappingTable(androidTreeRoot):
    system_audio_include_dir = androidTreeRoot + "/system/media/audio/include/system/"
    android_audio_hal_header_file = system_audio_include_dir + os.path.basename("audio-hal-enums.h")
    android_audio_common_base_header_file = system_audio_include_dir + os.path.basename("audio_common-base.h")
    logging.debug("android_audio_hal_header_file={}".format(android_audio_hal_header_file))
    if not os.path.exists(os.path.dirname(android_audio_hal_header_file)):
        logging.error("Could not find file".format(android_audio_hal_header_file))
        sys.exit(main())
    if not os.path.exists(os.path.dirname(android_audio_common_base_header_file)):
        logging.error("Could not find file".format(android_audio_common_base_header_file))
        sys.exit(main())
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

    logging.info("Checking Android Common Header file {}".format(android_audio_hal_header_file))

    criteria_pattern = re.compile(
        r"\s*V\((?P<type>(?:"+'|'.join(criterion_mapping_table.keys()) + "))_" \
        r"(?P<literal>(?!" + '|'.join(ignored_values) + ")\w*)\s*,\s*" \
        r"(?:AUDIO_DEVICE_BIT_IN \| )?(?P<values>(?:0[xX])?[0-9a-fA-F]+|[0-9]+)")

    with open(android_audio_hal_header_file, "r") as fp:
        for line in fp:
            match = criteria_pattern.match(line)
            if match:
                criterion_name = criterion_mapping_table[match.groupdict()['type']]
                legacy_criterion_literal = \
                    ''.join((w.capitalize() for w in match.groupdict()['literal'].split('_')))
                aidl_criterion_literal = ''.join(match.groupdict()['literal'])

                all_criteria[criterion_name][str(legacy_criterion_literal)] = str(aidl_criterion_literal)

                logging.debug("type:{},".format(criterion_name))
                logging.debug("legacy:{},".format(legacy_criterion_literal))
                logging.debug("aidl:{}.".format(aidl_criterion_literal))

    logging.info("Checking Android Common Header file {}".format(android_audio_common_base_header_file))

    criteria_pattern = re.compile(
        r"\s*(?P<type>(?:"+'|'.join(criterion_mapping_table.keys()) + "))_" \
        r"(?P<literal>(?!" + '|'.join(ignored_values) + ")\w*)\s*=\s*" \
        r"(?:AUDIO_DEVICE_BIT_IN \| )?(?P<values>(?:0[xX])?[0-9a-fA-F]+|[0-9]+)")

    with open(android_audio_common_base_header_file, "r") as fp:
        for line in fp:
            match = criteria_pattern.match(line)
            if match:
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
    all_criteria['ForceUseForHdmiSystemAudio'] = {'ForceNone' : "NONE", 'ForceHdmiSystemEnforced' : "HDMI_SYSTEM_AUDIO_ENFORCED"}
    all_criteria['ForceUseForEncodedSurround'] = {'ForceNone' : "NONE", 'ForceEncodedSurroundNever' : "ENCODED_SURROUND_NEVER", 'ForceEncodedSurroundAlways' : "ENCODED_SURROUND_ALWAYS", 'ForceEncodedSurroundManual' : "ENCODED_SURROUND_MANUAL"}
    all_criteria['ForceUseForVibrateRinging'] = {'ForceNone' : "NONE", 'ForceBtSco' : "BT_SCO"}

    return all_criteria

def parseLegacyStrategyAidlIds(androidTreeRoot):
    hidl_strategy_aidl_ids_dir = androidTreeRoot + "system/hardware/interfaces/media/aidl/android/media/audio/common/"
    hidl_strategy_aidl_ids_file = hidl_strategy_aidl_ids_dir + os.path.basename("AudioProductStrategyType.aidl")
    logging.debug("hidl_strategy_aidl_ids_file={}".format(hidl_strategy_aidl_ids_file))
    if not os.path.exists(os.path.dirname(hidl_strategy_aidl_ids_file)):
        logging.error("Could not find legacy strategies AIDL ids file".format(hidl_strategy_aidl_ids_file))
        sys.exit(main())

    legacy_strategy_id_to_name_map = {}
    legacy_strategy_pattern = re.compile(
            r"\s*(?P<name>(?:[A-Z_]*))\s*=" \
            r"\s*(?P<id>(?:\d*)),")
    with open(hidl_strategy_aidl_ids_file, "r") as fp:
        for line in fp:
            # Line with criterion?
            match = legacy_strategy_pattern.match(line)
            if match:
                parsed_legacy_name = match.groupdict()['name']
                if parsed_legacy_name.startswith("SYS_RESERVED_"):
                    cleaned_parsed_legacy_name = parsed_legacy_name[len("SYS_RESERVED_"):]
                else:
                    cleaned_parsed_legacy_name = parsed_legacy_name
                legacy_strategy_name = 'STRATEGY_' + cleaned_parsed_legacy_name
                legacy_strategy_id = match.groupdict()['id']
                legacy_strategy_id_to_name_map[legacy_strategy_name] = legacy_strategy_id

    return legacy_strategy_id_to_name_map

def convertStrategyEngineConfigFile(legacyEngineStrategyConfiguration, legacy_strategy_id_to_name_map, outputDir):
    logging.info("Reading Legacy Engine Strategy Configuration file {}".format(legacyEngineStrategyConfiguration))

    hidl_strategy_config_tree = ET.parse(legacyEngineStrategyConfiguration)
    hidl_strategy_config_root = hidl_strategy_config_tree.getroot()
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

    for strategy in hidl_strategy_config_root.findall('ProductStrategy'):
        name = strategy.get('name')
        if str(name) in legacy_strategy_id_to_name_map.keys():
            strategy.set('id', str(legacy_strategy_id_to_name_map[name]))
        else:
            strategy.set('id', str(vendor_strategy_id))
            strategy_id_to_name_map[name] = vendor_strategy_id
            vendor_strategy_id += 1

        if vendor_strategy_id == 1040:
            logging.error("Reaching max supported strategies by AIDL Cap Engine".format(vendor_strategy_id))
            sys.exit(main())

    with open(aidl_engine_strategy_config_file, "w") as f:
        hidl_strategy_config_tree.write(aidl_engine_strategy_config_file, encoding='UTF-8', xml_declaration=True)
    return strategy_id_to_name_map

def convertPfwEddFiles(vendor_strategy_id_to_name_map, legacy_strategy_id_to_name_map, legacy_to_aidl_criterion_map, legacyPFwEdds, outputDir):
    aidl_pfw_settings_dir = outputDir + "/policy/engine/parameter-framework/Settings/"

    for legacy_edd_file in legacyPFwEdds:
        logging.debug("legacy_edd_file={}".format(legacy_edd_file))
        logging.debug("basename={}".format(os.path.basename(legacy_edd_file.name)))
        aidl_edd_file = aidl_pfw_settings_dir + os.path.basename(legacy_edd_file.name)
        logging.debug("aidl_edd_file={}".format(aidl_edd_file))
        if not os.path.exists(os.path.dirname(aidl_edd_file)):
            try:
                os.makedirs(os.path.dirname(aidl_edd_file))
            except OSError as exc: # Guard against race condition
                if exc.errno != errno.EEXIST:
                    raise

        criterion_rules = { 'Is', 'IsNot', 'Excludes', 'Includes'}
        criterion_rule_pattern = re.compile(
                r"\s*(?P<type>(?:"+'|'.join(legacy_to_aidl_criterion_map.keys()) + "))" \
                r"\s*(?P<rule>(?:"+'|'.join(criterion_rules) + " ))" \
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
                    logging.debug("type={}".format(criterion))
                    logging.debug("rule={}".format(match.groupdict()['rule']))
                    logging.debug("values={}".format(legacy_value))

                    if str(legacy_value) in legacy_to_aidl_criterion_map[criterion].keys():
                        logging.debug("valid legacy value {} mapped with aidl {}".format(legacy_value, legacy_to_aidl_criterion_map[criterion][legacy_value]))
                    else:
                        logging.error("Unsupported legacy value found {}".format(legacy_value))
                        sys.exit(main())

                    if criterion == "AvailableInputDevices":
                        # Remove ambient and in_communication since they were deprecated
                        if legacy_value == "Ambient" or legacy_value == "Communication":
                            logging.error(" deprecated device {}, fix legacy config first".format(legacy_value))
                            # sys.exit(main())
                            continue

                    aidl_edd_line = line.replace(legacy_value, legacy_to_aidl_criterion_map[criterion][legacy_value])
                    logging.debug("aidl_edd_line={}".format(aidl_edd_line))
                    f.write(aidl_edd_line)

                elif any(pattern in line for pattern in strategy_param_pattern):
                    matching_vendor_strategy = next((pattern for pattern in vendor_strategy_id_to_name_map.keys() if pattern in line), None)
                    matching_legacy_strategy = next((pattern for pattern in legacy_strategy_id_to_name_map.keys() if pattern in line), None)
                    if matching_vendor_strategy:
                        aidl_edd_line = line.replace(matching_vendor_strategy, 'vx_' + str(vendor_strategy_id_to_name_map[matching_vendor_strategy]))
                        f.write(aidl_edd_line)
                    elif matching_legacy_strategy:
                        logging.debug(" keeping legacy strategy name for strategy parameters {}".format(line))
                        f.write(line)
                    else:
                        f.write(line)

                elif "ambient =" in line:
                    logging.error(" deprecated ambient parameter, fix legacy config first")
                    #sys.exit(main())
                    continue
                elif "communication =" in line:
                    logging.error(" deprecated communication parameter, fix legacy config first")
                    #sys.exit(main())
                    continue
                else:
                    # copy line without changes
                    f.write(line)
            # Line with strategy parameter path?


def main():
    logging.root.setLevel(logging.INFO)
    args = parseArgs()

    legacy_strategy_id_to_name_map = parseLegacyStrategyAidlIds(args.androidTreeRoot)

    logging.debug("legacy_strategy_id_to_name_map={} ".format(legacy_strategy_id_to_name_map))

    vendor_strategy_id_to_name_map = convertStrategyEngineConfigFile(args.legacyEngineStrategyConfiguration,
                                                                     legacy_strategy_id_to_name_map,
                                                                     args.outputDir)
    logging.debug("vendor_strategy_id_to_name_map={} ".format(vendor_strategy_id_to_name_map))

    legacy_to_aidl_criterion_map = generateLegacyToAidlMappingTable(args.androidTreeRoot)

    logging.debug("legacy_to_aidl_criterion_map={} ".format(legacy_to_aidl_criterion_map))

    convertPfwEddFiles(vendor_strategy_id_to_name_map, legacy_strategy_id_to_name_map, legacy_to_aidl_criterion_map, args.edd_files, args.outputDir)

# If this file is directly executed
if __name__ == "__main__":
    sys.exit(main())
