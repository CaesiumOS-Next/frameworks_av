/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.media.benchmark.library;

import android.util.Log;

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Measures Performance.
 */
public class Stats {
    private static final String TAG = "Stats";
    private long mLaunchTimeNs;
    private long mInitTimeNs;
    private long mDeInitTimeNs;
    private long mStartTimeNs;
    private ArrayList<Integer> mFrameSizes;
    /*
     * Array for holding the wallclock time
     * for each input buffer available.
     */
    private ArrayList<Long> mInputTimer;
    /*
     * Array for holding the wallclock time
     * for each output buffer available.
     * This is used for determining the decoded
     * frame intervals.
     */
    private ArrayList<Long> mOutputTimer;

    public Stats() {
        mFrameSizes = new ArrayList<>();
        mInputTimer = new ArrayList<>();
        mOutputTimer = new ArrayList<>();
        mLaunchTimeNs = 0;
        mInitTimeNs = 0;
        mDeInitTimeNs = 0;
    }

    public long getCurTime() { return System.nanoTime(); }

    public void setLaunchTimeNs(long launchTimeNs) { mLaunchTimeNs = launchTimeNs; }

    public void setInitTime(long initTime) { mInitTimeNs = initTime; }

    public void setDeInitTime(long deInitTime) { mDeInitTimeNs = deInitTime; }

    public void setStartTime() { mStartTimeNs = System.nanoTime(); }

    public void addFrameSize(int size) { mFrameSizes.add(size); }

    public void addInputTime() { mInputTimer.add(System.nanoTime()); }

    public void addOutputTime() { mOutputTimer.add(System.nanoTime()); }

    public void reset() {
        if (mFrameSizes.size() != 0) {
            mFrameSizes.clear();
        }

        if (mInputTimer.size() != 0) {
            mInputTimer.clear();
        }

        if (mOutputTimer.size() != 0) {
            mOutputTimer.clear();
        }
    }

    public long getInitTime() { return mInitTimeNs; }

    public long getDeInitTime() { return mDeInitTimeNs; }

    public long getStartTime() { return mStartTimeNs; }

    public List<Long> getOutputTimers() { return mOutputTimer; }

    public List<Long> getInputTimers() { return mInputTimer; }

    public long getTimeDiff(long sTime, long eTime) { return (eTime - sTime); }

    public long getTotalTime() {
        if (mOutputTimer.size() == 0) {
            return -1;
        }
        long lastTime = mOutputTimer.get(mOutputTimer.size() - 1);
        return lastTime - mStartTimeNs;
    }

    public long getTotalSize() {
        long totalSize = 0;
        for (long size : mFrameSizes) {
            totalSize += size;
        }
        return totalSize;
    }

    /**
     * Format time in appropriate units
     */
    private String formatTime(long nanoseconds) {
        if (nanoseconds == 0) return "0";
        double ms = nanoseconds / 1000000.0;
        return String.format("%.3f", ms);
    }

    private String formatTime(double nanoseconds) {
        if (nanoseconds == 0) return "0";
        double ms = nanoseconds / 1000000.0;
        return String.format("%.3f", ms);
    }


    /**
     * Writes the stats header to a file
     * <p>
     * \param statsFile    file where the stats data is to be written
     **/
    public boolean writeStatsHeader(String statsFile) throws IOException {
        File outputFile = new File(statsFile);
        FileOutputStream out = new FileOutputStream(outputFile, true);
        if (!outputFile.exists())
            return false;
        /*String statsHeader =
                "currentTime, fileName, operation, componentName, NDK/SDK, sync/async, setupTime, "
                        + "destroyTime, minimumTime, maximumTime, "
                        + "averageTime, timeToProcess1SecContent, totalBytesProcessedPerSec, "
                        + "timeToFirstFrame, totalSizeInBytes, totalTime\n";
        out.write(statsHeader.getBytes());*/
        out.close();
        return true;
    }

    /**
     * Dumps the stats of the operation for a given input media.
     * <p>
     * \param inputReference input media
     * \param operation      describes the operation performed on the input media
     * (i.e. extract/mux/decode/encode)
     * \param componentName  name of the codec/muxFormat/mime
     * \param mode           the operating mode: sync/async.
     * \param durationUs     is a duration of the input media in microseconds.
     * \param statsFile      the file where the stats data is to be written.
     */
    public void dumpStatistics(String inputReference, String operation, String componentName,
            String mode, long durationUs, String statsFile) throws IOException {
        File outputFile = new File(statsFile);
        FileOutputStream out = new FileOutputStream(outputFile, true);
        StringBuilder summary = new StringBuilder();
        summary.append(String.format("filename: %s\ncodec name: %s\nmode: %s\n", inputReference,
                componentName, mode));
        summary.append(String.format(
                "Start Time ms (Time taken to create configure start the codec and get first "
                        + "valid output buffer to queue):, %s\n -> Time taken to create configure and "
                        + "start codec (ms):, %s\n ->Time Taken to get the first valid output buffer to "
                        + "queue after start (ms):, %s\n",
                formatTime(mOutputTimer.getFirst() - mLaunchTimeNs), formatTime(mInitTimeNs),
                formatTime(mOutputTimer.getFirst() - mStartTimeNs)));

        StringBuilder log = new StringBuilder();
        log.append("\n, ");
        log.append("index, input enqueue time ms, output dequeue time ms, enqueue successive diff"
                + " ms, dequeue successive diff ms \n");
        int inputSize = mInputTimer.size();
        int outputSize = mOutputTimer.size();
        log.append(String.format(",%d, %s, %s\n", 0, formatTime(mInputTimer.getFirst()),
                formatTime(mOutputTimer.getFirst())));
        double[] enqueueTimes = new double[3];
        Arrays.fill(enqueueTimes, 0);
        enqueueTimes[0] = Float.MAX_VALUE;
        double[] dequeueTimes = new double[3];
        Arrays.fill(dequeueTimes, 0);
        dequeueTimes[0] = Float.MAX_VALUE;
        int index;
        for (index = 1; index < (Math.max(inputSize, outputSize) - 1); index++) {
            long inputTimerLast = mInputTimer.get(Math.min(index - 1, inputSize - 1));
            long inputTimerCurrent = mInputTimer.get(Math.min(index, inputSize - 1));
            long enqueueDiff = (inputTimerCurrent - inputTimerLast);
            if (enqueueDiff != 0) {
                enqueueTimes[0] = Math.min(enqueueTimes[0], enqueueDiff);
            }
            enqueueTimes[1] = Math.max(enqueueTimes[1], enqueueDiff);
            enqueueTimes[2] += enqueueDiff;
            long outputTimerLast = mOutputTimer.get(Math.min(index - 1, outputSize - 1));
            long outputTimerCurrent = mOutputTimer.get(Math.min(index, outputSize - 1));
            long dequeueDiff = (outputTimerCurrent - outputTimerLast);
            if (dequeueDiff != 0) {
                dequeueTimes[0] = Math.min(dequeueTimes[0], dequeueDiff);
            }
            dequeueTimes[1] = Math.max(dequeueTimes[1], dequeueDiff);
            dequeueTimes[2] += dequeueDiff;
            log.append(String.format(",%d, %s, %s, %s, %s\n", index, formatTime(inputTimerCurrent),
                    formatTime(outputTimerCurrent), formatTime(enqueueDiff),
                    formatTime(dequeueDiff)));
        }
        enqueueTimes[2] /= index;
        dequeueTimes[2] /= index;
        summary.append(String.format("enqueue times ms : \nmin =, %s\n max =, %s\n avg =, %s\n",
                formatTime(enqueueTimes[0]), formatTime(enqueueTimes[1]),
                formatTime(enqueueTimes[2])));
        summary.append(String.format("dequeue times ms : \nmin =, %s\n max =, %s\n avg =, %s\n",
                formatTime(dequeueTimes[0]), formatTime(dequeueTimes[1]),
                formatTime(dequeueTimes[2])));
        String msg = summary.toString();
        out.write(msg.getBytes());
        msg = log.toString();
        out.write(msg.getBytes());
        out.close();
    }
}
