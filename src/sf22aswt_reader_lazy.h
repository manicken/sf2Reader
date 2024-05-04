#pragma once

#include <Arduino.h>
#include <SD.h>

#include "sf22aswt_common.h"
#include "sf22aswt_structures.h"
#include "sf22aswt_enums.h"
#include "sf22aswt_helpers.h"
#include "sf22aswt_converter.h"

namespace SF22ASWT
{
    class ReaderLazy : public SF22ASWT::common
    {
      public:
        sfbk_rec_lazy sfbk;
        bool lastReadWasOK = false;

        /** reads and verifies the sf2 file,
         *  note. this is lazy read 
         *  and only the file data position for
         *  all used blocks are stored into ram
         */
        bool ReadFile(const char * filePath)
        {
            lastReadWasOK = false;
            clearErrors();

            File file = SD.open(filePath);
            if (!file) {  lastError = Error::Errors::FILE_NOT_OPEN; return false; }
            fileSize = file.size();

            char fourCC[4];

            if ((lastReadCount = file.readBytes(fourCC, 4)) != 4) FILE_ERROR(FILE_FOURCC_READ) //("read error - while reading fileTag")
            if (verifyFourCC(fourCC) == false) FILE_ERROR(FILE_FOURCC_INVALID) //("error - invalid fileTag")
            if (strncmp(fourCC, "RIFF", 4) != 0) FILE_ERROR(FILE_FOURCC_MISMATCH) //("error - not a RIFF fileformat")
            
            if ((lastReadCount = file.read(&sfbk.size, 4)) != 4) FILE_ERROR(RIFF_SIZE_READ) //("read error - while reading RIFF size")
            if (fileSize != (sfbk.size + 8)) FILE_ERROR(RIFF_SIZE_MISMATCH) //("error - fileSize mismatch")

            if ((lastReadCount = file.readBytes(fourCC, 4)) != 4) FILE_ERROR(RIFF_FOURCC_READ) //("read error - while reading fileformat")
            if (verifyFourCC(fourCC) == false) FILE_ERROR(RIFF_FOURCC_INVALID) //("error - invalid fileformat")
            
            if (strncmp(fourCC, "sfbk", 4) != 0) FILE_ERROR(RIFF_FOURCC_MISMATCH) //("error - not a sfbk fileformat")

            char listTag[4];
            uint32_t listSize = 0;
            while (file.available() > 0)
            {
                // every block starts with a LIST tag
                if ((lastReadCount = file.readBytes(listTag, 4)) != 4) FILE_ERROR(LIST_FOURCC_READ) //("read error - while reading listTag")
                if (verifyFourCC(listTag) == false) FILE_ERROR(LIST_FOURCC_INVALID) //("error - listTag invalid")
                if (strncmp(listTag, "LIST", 4) != 0) FILE_ERROR(LIST_FOURCC_MISMATCH) //("error - listTag is not LIST")

                
                if ((lastReadCount = file.read(&listSize, 4)) != 4) FILE_ERROR(LIST_SIZE_READ) //("read error - while getting listSize")

                if ((lastReadCount = file.readBytes(fourCC, 4)) != 4) FILE_ERROR(LISTTYPE_FOURCC_READ) //("read error - while reading listType")
                DebugPrintFOURCC(fourCC);
                DebugPrintFOURCC_size(listSize);
                if (verifyFourCC(fourCC) == false) FILE_ERROR(LISTTYPE_FOURCC_INVALID) //("error - invalid listType")
                
                
                if (strncmp(fourCC, "INFO", 4) == 0)
                {
                    sfbk.info_position = file.position(); // normally don't read info chunk to save ram
                    sfbk.info_size = listSize;
                    if (file.seek(listSize - 4, SeekCur) == false) FILE_SEEK_ERROR(INFO_DATA_SKIP, listSize - 4) //("seek error - while skipping INFO block")
                    
                    //file.close(); return true; // early return debug test
                }
                else if (strncmp(fourCC, "sdta", 4) == 0)
                {
                    sfbk.sdta.size = listSize;
                    if (read_sdta_block(file, sfbk.sdta) == false) return false;
                    //file.close(); return true; // early return debug test
                }
                else if (strncmp(fourCC, "pdta", 4) == 0)
                {
                    sfbk.pdta.size = listSize;
                    if (read_pdta_block(file, sfbk.pdta) == false) return false;
                    //file.close(); return true; // early return debug test
                }
                else
                {
                    // normally unknown blocks should be ignored
                    if (file.seek(listSize - 4, SeekCur) == false) FILE_SEEK_ERROR(LIST_UNKNOWN_BLOCK_DATA_SKIP, listSize - 4) //("seek error - while skipping unknown sfbk root block")
                }
            }

            file.close();
            lastReadWasOK = true;
            this->filePath = filePath;
            return true;
        }

        void printInstrumentListAsJson(Stream &stream)
        {
            stream.print("json:{\"instruments\":[");
            File file = SD.open(filePath.c_str());
            file.seek(sfbk.pdta.inst_position);
            SF22ASWT::inst_rec inst;
            
            for (uint32_t i = 0; i < sfbk.pdta.inst_count - 1; i++) // -1 the last is allways a EOI
            {
                file.read(&inst, SF22ASWT::inst_rec::Size);
                stream.print("{\"name\":\"");
                Helpers::printRawBytesUntil(stream, inst.achInstName, 20, '\0');
                stream.print("\",\"ndx\":");
                stream.print(inst.wInstBagNdx);
                stream.print("},");
            }
            file.close();
            stream.println("]}");
        }

        void printPresetListAsJson(Stream &stream)
        {
            stream.print("json:{\"presets\":[");
            File file = SD.open(filePath.c_str());
            file.seek(sfbk.pdta.phdr_position);
            SF22ASWT::phdr_rec phdr;
            
            for (uint32_t i = 0; i < sfbk.pdta.phdr_count - 1; i++) // -1 the last is allways a EOP
            {
                file.read(&phdr, SF22ASWT::phdr_rec::Size);
                stream.print("{\"name\":\"");
                Helpers::printRawBytesUntil(stream, phdr.achPresetName, 20, '\0');
                stream.print("\",\"bank\":");
                stream.print(phdr.wBank);
                stream.print(",\"preset\":");
                stream.print(phdr.wPreset);
                stream.print(",\"bagNdx\":");
                stream.print(phdr.wPresetBagNdx);
                stream.print("},");
            }
            file.close();
            stream.println("]}");
        }

        /**
         * this function do only load the sample preset headers for the instrument
         * to load the actual sample data the function SF22ASWT::ReadSampleDataFromFile
         * @'common.h' should be used
        */
        bool load_instrument_data(uint index, SF22ASWT::instrument_data_temp &inst)
        {
            clearErrors();

            if (index > sfbk.pdta.inst_count - 1){ 
                //lastError = "load instrument index out of range";
                lastError = Error::Errors::FUNCTION_LOAD_INST_INDEX_RANGE;
                return false;
            }

            File file = SD.open(filePath.c_str());
            uint64_t seekPos = sfbk.pdta.inst_position + inst_rec::Size*index + 20;
            if (file.seek(seekPos) == false) FILE_SEEK_ERROR(PDTA_INST_DATA_SEEK, seekPos)
            uint16_t ibag_startIndex = 0;
            uint16_t ibag_endIndex = 0;
            if ((lastReadCount = file.read(&ibag_startIndex, 2)) != 2) FILE_ERROR(PDTA_INST_DATA_READ)
            // skipping next inst name
            if (file.seek(20, SeekCur) == false) FILE_SEEK_ERROR(PDTA_INST_DATA_SKIP, 20)
            if ((lastReadCount = file.read(&ibag_endIndex, 2)) != 2) FILE_ERROR(PDTA_INST_DATA_READ)
            
            DebugPrint_Text_Var("\nibag_start index: ", ibag_startIndex);
            DebugPrintln_Text_Var(", ibag_end index: ", ibag_endIndex);
            DebugPrint("\n");
            seekPos = sfbk.pdta.ibag_position + bag_rec::Size*ibag_startIndex;
            if (file.seek(seekPos) == false) FILE_SEEK_ERROR(PDTA_IBAG_DATA_SEEK, seekPos) //seek error to ibags
            DebugPrint("igen_ndxs: ");
            uint16_t ibag_count = ibag_endIndex - ibag_startIndex; 
            uint16_t igen_ndxs[ibag_count+1]; // +1 because of the soundfont structure 
            uint16_t dummy = 0;
            for (int i=0;i<ibag_count+1;i++)
            {
                if ((lastReadCount = file.read(&igen_ndxs[i], 2)) != 2) FILE_ERROR(PDTA_IBAG_DATA_READ) //read error - while reading &igen_ndxs[i]
                if ((lastReadCount = file.read(&dummy, 2)) != 2) FILE_ERROR(PDTA_IBAG_DATA_SKIP) //read error - while reading dummy
                DebugPrint(igen_ndxs[i]);
                DebugPrint(", ");
            }
            DebugPrint("\n");
            // store gen data in bags for faster access
            bag_of_gens bags[ibag_count];

            // search to the location for the first igen record
            seekPos = sfbk.pdta.igen_position + igen_ndxs[0]*gen_rec::Size;
            if (file.seek(seekPos) == false) FILE_SEEK_ERROR(PDTA_IGEN_DATA_SEEK, seekPos) //seek error to first igen record
                
            for (int i=0;i<ibag_count;i++)
            {
                uint16_t start = igen_ndxs[i];
                uint16_t end = igen_ndxs[i+1];
                uint16_t count = end-start;
                bags[i].items = new gen_rec[count];
                bags[i].count = count;

                if ((lastReadCount = file.read(bags[i].items, gen_rec::Size*count)) != gen_rec::Size*count) FILE_ERROR(PDTA_IGEN_DATA_READ)
                
                DebugPrint("bag contents:\n");
    #ifdef DEBUG
                for (int i2=0;i2<count;i2++)
                {
                    DebugPrint_Text_Var("  sfGenOper:", (uint16_t)bags[i].items[i2].sfGenOper);
                    DebugPrintln_Text_Var(", value:", bags[i].items[i2].genAmount.UAmount);
                }
    #endif
                
            }
            DebugPrint("temp storage in bags complete\n");

            // if the first zone ends with a sampleID gen type then there is not any global zone for that instrument
            bool globalExists = (bags[0].count != 0)?(bags[0].lastItem().sfGenOper != SFGenerator::sampleID):true;

            inst.sample_count = globalExists?(ibag_count - 1):ibag_count;

            inst.sample_note_ranges = new uint8_t[inst.sample_count];
            inst.samples = new sample_header_temp[inst.sample_count];
            DebugPrint("\nsample count: "); DebugPrint(inst.sample_count);
            for (int si=0;si<inst.sample_count;si++)
            {
                shdr_rec shdr;
                DebugPrintln_Text_Var("getting sample x: ", si);
                if (sample_header(file, sfbk, bags, si, &shdr) == false) { 
                    break; // classify the file as structually unsound
                    //inst.samples[si].invalid = true;
                    DebugPrintln_Text_Var("error - while getting sample header @ ", si);
                    //continue;
                }
                DebugPrint("sample name: ");
    #ifdef DEBUG
                Helpers::printRawBytesSanitizedUntil(USerial, shdr.achSampleName, 20, '\0');
    #endif
                DebugPrintln();
                DebugPrintln("getting data:");
                inst.sample_note_ranges[si] = key_range_end(bags, si);
                inst.samples[si].sample_start = shdr.dwStart*2 + sfbk.sdta.smpl.position;
                inst.samples[si].LOOP = sample_repeat(bags, si, false);
                inst.samples[si].SAMPLE_NOTE = sample_note(bags, si, shdr);
                inst.samples[si].CENTS_OFFSET = fine_tuning(bags, si);
                inst.samples[si].LENGTH = length(bags, si, shdr);
                inst.samples[si].LENGTH_BITS = length_bits(inst.samples[si].LENGTH);
                inst.samples[si].SAMPLE_RATE = shdr.dwSampleRate;
                inst.samples[si].LOOP_START = cooked_loop_start(bags, si, shdr);
                inst.samples[si].LOOP_END = cooked_loop_end(bags, si, shdr);
                inst.samples[si].INIT_ATTENUATION = decibel_value(bags, si, SFGenerator::initialAttenuation, 0, 0, 144) * -1;
                DebugPrintln("getting vol env");
                // VOLUME ENVELOPE VALUES
                inst.samples[si].DELAY_ENV = timecents_value(bags, si, SFGenerator::delayVolEnv, 0, 0);
                inst.samples[si].ATTACK_ENV = timecents_value(bags, si, SFGenerator::attackVolEnv, 1, 1);
                inst.samples[si].HOLD_ENV = timecents_value(bags, si, SFGenerator::holdVolEnv, 0, 0);
                inst.samples[si].DECAY_ENV = timecents_value(bags, si, SFGenerator::decayVolEnv, 1, 1);
                inst.samples[si].RELEASE_ENV = timecents_value(bags, si, SFGenerator::releaseVolEnv, 1, 1);
                inst.samples[si].SUSTAIN_FRAC = decibel_value(bags, si, SFGenerator::sustainVolEnv, 0, 0, 144) * -1;
                DebugPrintln("getting vib vals");
                // VIRBRATO VALUES
                inst.samples[si].VIB_DELAY_ENV = timecents_value(bags, si, SFGenerator::delayVibLFO, 0, 0);
                inst.samples[si].VIB_INC_ENV = hertz(bags, si, SFGenerator::freqVibLFO, 8.176, 0.1, 100);
                inst.samples[si].VIB_PITCH_INIT = pitch_cents(bags, si, SFGenerator::vibLfoToPitch, 0, -12000, 12000);
                inst.samples[si].VIB_PITCH_SCND = inst.samples[si].VIB_PITCH_INIT * -1; //pitch_cents(bags, si, SFGenerator::vibLfoToPitch, 0, -12000, 12000) * -1;
                DebugPrintln("getting mod vals");
                // MODULATION VALUES
                inst.samples[si].MOD_DELAY_ENV = timecents_value(bags, si, SFGenerator::delayModLFO, 0, 0);
                inst.samples[si].MOD_INC_ENV = hertz(bags, si, SFGenerator::freqModLFO, 8.176, 0.1, 100);
                inst.samples[si].MOD_PITCH_INIT = pitch_cents(bags, si, SFGenerator::modLfoToPitch, 0, -12000, 12000);
                inst.samples[si].MOD_PITCH_SCND = inst.samples[si].MOD_PITCH_INIT * -1; //pitch_cents(bags, si, SFGenerator::modLfoToPitch, 0, -12000, 12000) * -1;
                inst.samples[si].MOD_AMP_INIT_GAIN = decibel_value(bags, si, SFGenerator::modLfoToVolume, 0, -96, 96);
                inst.samples[si].MOD_AMP_SCND_GAIN = inst.samples[si].MOD_AMP_INIT_GAIN * -1; //decibel_value(bags, si, SFGenerator::modLfoToVolume, 0, -96, 96) * -1;
            }

            // Deallocate memory for bags_of_gens
            for (int i = 0; i < ibag_count; i++) {
                delete[] bags[i].items; // Deallocate memory for the array of pointers
            }
            
            file.close();
            return true;
        }

        /**
         * this is mostly intended as a demo or to quickly use this library
        */
        bool load_instrument_from_file(const char * filePath, int instrumentIndex, AudioSynthWavetable::instrument_data **aswt_id)
        {
            
            if (ReadFile(filePath) == false)
            {
                USerial.println("Read file error:");
                printSF2ErrorInfo();
                return false;
            }
            SF22ASWT::instrument_data_temp inst_temp = {0,0,nullptr};

            if (load_instrument_data(instrumentIndex, inst_temp) == false)
            {
                USerial.println("load_instrument_data error:");
                printSF2ErrorInfo();
                return false;
            }
            if (ReadSampleDataFromFile(inst_temp) == false)
            {
                USerial.println("ReadSampleDataFromFile error:");
                printSF2ErrorInfo();
                return false;
            }
            AudioSynthWavetable::instrument_data* new_inst = new AudioSynthWavetable::instrument_data(SF22ASWT::converter::to_AudioSynthWavetable_instrument_data(inst_temp));
            if (new_inst == nullptr) // failsafe
            {
                USerial.println("convert to AudioSynthWavetable::instrument_data error!");
                return false;
            }
            
            *aswt_id = new_inst;
            return true;
        }

        
        void PrintInfoBlock(Print &stream)
        {
            SF22ASWT::INFO info;
            File file = SD.open(filePath.c_str());
            file.seek(sfbk.info_position);
            readInfoBlock(file, info);
            
            file.close();
            info.size = sfbk.info_size;
            info.Print(stream);
        }

  private:
        
    #pragma region read_blocks
        /// <summary>
        /// reads data offset pointers and sizes of sample data, not the actual data
        /// as the data is read from file on demand
        /// </summary>
        /// <param name="br"></param>
        /// <returns></returns>
        bool read_sdta_block(File &file, sdta_rec_lazy &sdta)
        {
            char fourCC[4];
            while (file.available())
            {
                if ((lastReadCount = file.readBytes(fourCC, 4)) != 4) FILE_ERROR(SDTA_FOURCC_READ) //("read error - while getting sdtablock type")
                DebugPrintFOURCC(fourCC);
                if (verifyFourCC(fourCC) == false) FILE_ERROR(SDTA_FOURCC_INVALID) //("error - sdtablock type invalid")
                
                if (strncmp(fourCC, "smpl", 4) == 0)
                {
                    if ((lastReadCount = file.read(&sdta.smpl.size, 4)) != 4) FILE_ERROR(SDTA_SMPL_SIZE_READ) //("read error - while reading smpl size")
                    sdta.smpl.position = file.position();
                    // skip sample data
                    if (file.seek(sdta.smpl.size, SeekCur) == false) FILE_SEEK_ERROR(SDTA_SMPL_DATA_SKIP, sdta.smpl.size) //("seek error - while skipping smpl data")
                }
                else if (strncmp(fourCC, "sm24", 4) == 0)
                {
                    if ((lastReadCount = file.read(&sdta.sm24.size, 4)) != 4) FILE_ERROR(SDTA_SM24_SIZE_READ) //("read error - while reading sm24 size")
                    sdta.sm24.position = file.position();
                    // skip sample data
                    if (file.seek(sdta.sm24.size, SeekCur) == false) FILE_SEEK_ERROR(SDTA_SM24_DATA_SKIP, sdta.sm24.size) //("seek error - while skipping sm24 data")
                }
                else if (strncmp(fourCC, "LIST", 4) == 0)
                {
                    // skip back
                    if (file.seek(file.position()-4) == false) FILE_SEEK_ERROR(SDTA_BACK_SEEK, -4)
                    return true;
                }
                else
                {
                    // normally unknown blocks should be ignored
                    uint32_t size = 0;
                    if ((lastReadCount = file.read(&size, 4)) != 4) FILE_ERROR(SDTA_UNKNOWN_BLOCK_SIZE_READ) //("read error - while getting unknown sdta block size")
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(SDTA_UNKNOWN_BLOCK_DATA_SKIP, size) //("seek error - while skipping unknown sdta block")
                }
            }
            return true;
        }

        bool read_pdta_block(File &file, pdta_rec_lazy &pdta)
        {
            char fourCC[4];
            uint32_t size = 0;
            while (file.available())
            {
                if ((lastReadCount = file.readBytes(fourCC, 4)) != 4) FILE_ERROR(PDTA_FOURCC_READ) //("read error - while getting pdta block type")
                DebugPrintFOURCC(fourCC);
                if (verifyFourCC(fourCC) == false) FILE_ERROR(PDTA_FOURCC_INVALID) //("error - pdta type invalid")

                // store result for easier pinpoint of error handing
                bool sizeReadFail = ((lastReadCount = file.read(&size, 4)) != 4);

                if (strncmp(fourCC, "phdr", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_PHDR_SIZE_READ) //("read error - while getting pdta phdr block size")
                    if (size % phdr_rec::Size != 0) FILE_ERROR(PDTA_PHDR_SIZE_MISMATCH) //("error - pdta phdr block size mismatch")

                    pdta.phdr_count = size/phdr_rec::Size;
                    pdta.phdr_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_PHDR_DATA_SKIP, size) //("seek error - while skipping phdr block")
                }
                else if (strncmp(fourCC, "pbag", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_PBAG_SIZE_READ) //("read error - while getting pdta pbag block size")
                    if (size % bag_rec::Size != 0) FILE_ERROR(PDTA_PBAG_SIZE_MISMATCH) //("error - pdta pbag block size mismatch")

                    pdta.pbag_count = size/bag_rec::Size;
                    pdta.pbag_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_PBAG_DATA_SKIP, size) //("seek error - while skipping pbag block")
                }
                else if (strncmp(fourCC, "pmod", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_PMOD_SIZE_READ) //("read error - while getting pdta pmod block size")
                    if (size % mod_rec::Size != 0) FILE_ERROR(PDTA_PMOD_SIZE_MISMATCH) //("error - pdta pmod block size mismatch")

                    pdta.pmod_count = size/mod_rec::Size;
                    pdta.pmod_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_PMOD_DATA_SKIP, size) //("seek error - while skipping pmod block")
                }
                else if (strncmp(fourCC, "pgen", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_PGEN_SIZE_READ) //("read error - while getting pdta pgen block size")
                    if (size % gen_rec::Size != 0) FILE_ERROR(PDTA_PGEN_SIZE_MISMATCH) //("error - pdta pgen block size mismatch")

                    pdta.pgen_count = size/gen_rec::Size;
                    pdta.pgen_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_PGEN_DATA_SKIP, size) //("seek error - while skipping pgen block")
                }
                else if (strncmp(fourCC, "inst", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_INST_SIZE_READ) //("read error - while getting pdta inst block size")
                    if (size % inst_rec::Size != 0) FILE_ERROR(PDTA_INST_SIZE_MISMATCH) //("error - pdta inst block size mismatch")

                    pdta.inst_count = size/inst_rec::Size;
                    pdta.inst_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_INST_DATA_SKIP, size) //("seek error - while skipping inst block")
                }
                else if (strncmp(fourCC, "ibag", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_IBAG_SIZE_READ) //("read error - while getting pdta ibag block size")
                    if (size % bag_rec::Size != 0) FILE_ERROR(PDTA_IBAG_SIZE_MISMATCH) //("error - pdta ibag block size mismatch")

                    pdta.ibag_count = size/bag_rec::Size;
                    pdta.ibag_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_IBAG_DATA_SKIP, size) //("seek error - while skipping ibag block")
                }
                else if (strncmp(fourCC, "imod", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_IMOD_SIZE_READ) //("read error - while getting pdta imod block size")
                    if (size % mod_rec::Size != 0) FILE_ERROR(PDTA_IMOD_SIZE_MISMATCH) //("error - pdta imod block size mismatch")

                    pdta.imod_count = size/mod_rec::Size;
                    pdta.imod_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_IMOD_DATA_SKIP, size) //("seek error - while skipping imod block")
                }
                else if (strncmp(fourCC, "igen", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_IGEN_SIZE_READ) //("read error - while getting pdta igen block size")
                    if (size % gen_rec::Size != 0) FILE_ERROR(PDTA_IGEN_SIZE_MISMATCH) //("error - pdta igen block size mismatch")

                    pdta.igen_count = size/gen_rec::Size;
                    pdta.igen_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_IGEN_DATA_SKIP, size) //("seek error - while skipping igen block")
                }
                else if (strncmp(fourCC, "shdr", 4) == 0)
                {
                    if (sizeReadFail) FILE_ERROR(PDTA_SHDR_SIZE_READ) //("read error - while getting pdta shdr block size")
                    if (size % shdr_rec::Size != 0) FILE_ERROR(PDTA_SHDR_SIZE_MISMATCH) //("error - pdta shdr block size mismatch")

                    pdta.shdr_count = size/shdr_rec::Size;
                    pdta.shdr_position = file.position();
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_SHDR_DATA_SKIP, size) //("seek error - while skipping shdr block")
                }
                else if (strncmp(fourCC, "LIST", 4) == 0) // failsafe if file don't follow standard
                {
                    // skip back
                    if (file.seek(file.position()-8) == false) FILE_SEEK_ERROR(PDTA_BACK_SEEK, -8) 
                    return true;
                }
                else
                {
                    // normally unknown blocks should be ignored
                    if (sizeReadFail) FILE_ERROR(PDTA_UNKNOWN_BLOCK_SIZE_READ) //("read error - while getting unknown block size")
                    if (file.seek(size, SeekCur) == false) FILE_SEEK_ERROR(PDTA_UNKNOWN_BLOCK_DATA_SKIP, size) //("seek error - while skipping unknown pdta block")
                }
            }
            return true;
        }
    #pragma endregion

    #pragma region gen_get
        bool parameter_value(bag_of_gens* bags, int sampleIndex, SFGenerator genType, SF2GeneratorAmount *amount)
        {
            bool globalExists = (bags[0].count != 0)?(bags[0].lastItem().sfGenOper != SFGenerator::sampleID):true;
            int bagIndex = globalExists?(sampleIndex+1):sampleIndex;

            uint16_t sampleGenCount = bags[bagIndex].count;
            for (int i=0;i<sampleGenCount;i++)
            {
                if (bags[bagIndex].items[i].sfGenOper == genType) {
                    *amount = bags[bagIndex].items[i].genAmount;
                    return true;
                }
            }
            if (globalExists == false) return false;
            // try again with global bag
            uint16_t globalGenCount = bags[0].count;
            for (int i = 0; i < globalGenCount; i++)
            {
                if (bags[0].items[i].sfGenOper == genType) {
                    *amount = bags[0].items[i].genAmount;
                    return true;
                }
            }
            return false;
        }
        float decibel_value(bag_of_gens* bags, int sampleIndex, SFGenerator genType, float DEFAULT, float MIN, float MAX)
        {
            SF2GeneratorAmount genval;
            float val = parameter_value(bags, sampleIndex, genType, &genval)?genval.centibels(): DEFAULT;
            return (val > MAX) ? MAX : ((val < MIN) ? MIN : val);
        }
        float timecents_value(bag_of_gens* bags, int sampleIndex, SFGenerator genType, float DEFAULT, float MIN)
        {
            SF2GeneratorAmount genval;
            float val = parameter_value(bags, sampleIndex, genType, &genval)?genval.cents()*1000.0f: DEFAULT;
            return (val > MIN) ? val : MIN;
        }
        float hertz(bag_of_gens* bags, int sampleIndex, SFGenerator genType, float DEFAULT, float MIN, float MAX)
        {
            SF2GeneratorAmount genval;
            float val = parameter_value(bags, sampleIndex, genType, &genval)?genval.absolute_cents(): DEFAULT;
            return (val > MAX) ? MAX : ((val < MIN) ? MIN : val);
        }
        int pitch_cents(bag_of_gens* bags, int sampleIndex, SFGenerator genType, int DEFAULT, int MIN, int MAX)
        {
            SF2GeneratorAmount genval;
            int val = parameter_value(bags, sampleIndex, genType, &genval)?genval.Amount: DEFAULT;
            return (val > MAX) ? MAX : ((val < MIN) ? MIN : val);
        }
        int cooked_loop_start(bag_of_gens* bags, int sampleIndex, shdr_rec &shdr)
        {
            int result = (int)(shdr.dwStartloop - shdr.dwStart);
            SF2GeneratorAmount genval;
            result += parameter_value(bags, sampleIndex, SFGenerator::startloopAddrsOffset, &genval)?genval.Amount:0;
            result += parameter_value(bags, sampleIndex, SFGenerator::startloopAddrsCoarseOffset, &genval)?genval.coarse_offset():0;
            return result;
        }
        int cooked_loop_end(bag_of_gens* bags, int sampleIndex, shdr_rec &shdr)
        {
            int result = (int)(shdr.dwEndloop - shdr.dwStart);
            SF2GeneratorAmount genval;
            result += parameter_value(bags, sampleIndex, SFGenerator::endloopAddrsOffset, &genval)?genval.Amount:0;
            result += parameter_value(bags, sampleIndex, SFGenerator::endloopAddrsCoarseOffset, &genval)?genval.coarse_offset():0;
            return result;
        }
        int sample_note(bag_of_gens* bags, int sampleIndex, shdr_rec &shdr)
        {
            SF2GeneratorAmount genval;
            return parameter_value(bags, sampleIndex, SFGenerator::overridingRootKey, &genval)?genval.UAmount:((shdr.byOriginalKey <= 127)?shdr.byOriginalKey:60);
        }
        int fine_tuning(bag_of_gens* bags, int sampleIndex)
        {
            SF2GeneratorAmount genval;
            return parameter_value(bags, sampleIndex, SFGenerator::fineTune, &genval)?genval.Amount:0;
        }
        bool sample_header(File &file, sfbk_rec_lazy &sfbk, bag_of_gens* bags, int sampleIndex, shdr_rec *shdr)
        {
            SF2GeneratorAmount genval;
            if (parameter_value(bags, sampleIndex, SFGenerator::sampleID, &genval) == false) return false;
            uint64_t seekPos = sfbk.pdta.shdr_position + genval.UAmount*shdr_rec::Size;
            if (file.seek(seekPos) == false) FILE_SEEK_ERROR(PDTA_SHDR_DATA_SEEK, seekPos)
            if (file.read(shdr, shdr_rec::Size) != shdr_rec::Size) FILE_ERROR(PDTA_SHDR_DATA_READ)
            return true;
        }
        bool sample_repeat(bag_of_gens* bags, int sampleIndex, bool defaultValue)
        {
            SF2GeneratorAmount genVal;
            if (parameter_value(bags, sampleIndex, SFGenerator::sampleModes, &genVal) == false){ DebugPrintln("could not get samplemode"); return defaultValue; }
            
            return (genVal.sample_mode() == SFSampleMode::kLoopContinuously);// || (val.sample_mode == SampleMode.kLoopEndsByKeyDepression);
        }
        int length(bag_of_gens* bags, int sampleIndex, shdr_rec &shdr)
        {
            int length = (int)(shdr.dwEnd - shdr.dwStart);
            int cooked_loop_end_val = cooked_loop_end(bags, sampleIndex, shdr);
            if (sample_repeat(bags, sampleIndex, false) && cooked_loop_end_val < length)
            {
                return cooked_loop_end_val + 1;
            }
            return length;
        }
        int key_range_end(bag_of_gens* bags, int sampleIndex)
        {
            SF2GeneratorAmount genval;
            return parameter_value(bags, sampleIndex, SFGenerator::keyRange, &genval)?genval.rangeHigh():127;
        }
        int length_bits(int len)
        {
            int length_bits = 0;
            while (len != 0)
            {
                length_bits += 1;
                len = len >> 1;
            }
            return length_bits;
        }
    #pragma endregion
    };

// this is just a test
class MemoryStream : public Print {
  private:
    char *buffer;
    size_t bufferSize;
    size_t bufferIndex;

  public:
    MemoryStream(size_t size) {
      buffer = new char[size];
      bufferSize = size;
      bufferIndex = 0;
    }

    ~MemoryStream() {
      delete[] buffer;
    }

    virtual size_t write(uint8_t c) {
      if (bufferIndex < bufferSize - 1) {
        buffer[bufferIndex++] = c;
        buffer[bufferIndex] = '\0'; // Null-terminate the string
        return 1; // Successful write
      } else {
        return 0; // Buffer overflow
      }
    }

    virtual size_t write(const uint8_t *buffer, size_t size) {
      size_t bytesWritten = 0;
      for (size_t i = 0; i < size; ++i) {
        if (write(buffer[i]) == 1) {
          bytesWritten++;
        } else {
          break; // Stop writing on buffer overflow
        }
      }
      return bytesWritten;
    }

    const char *getData() {
      return buffer;
    }

    size_t getSize() {
      return bufferIndex;
    }

    void clear() {
      bufferIndex = 0;
      buffer[0] = '\0';
    }

        
  };
};