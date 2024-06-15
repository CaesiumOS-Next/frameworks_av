/*
 * Copyright 2024 The Android Open Source Project
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

#include <gtest/gtest.h>

#include <C2Buffer.h>
#include <_C2FenceFactory.h>

#include <sys/mman.h>
#include <ui/Fence.h>
#include <linux/kcmp.h>       /* Definition of KCMP_* constants */
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>

namespace android {

class C2FenceTest : public ::testing::Test {
public:
    C2FenceTest() = default;

    ~C2FenceTest() = default;


private:

};

// typedef _C2FenceFactory::SyncFenceImpl SyncFence;

// Sync fences can only be created with:
//   _C2FenceFactory::CreateSyncFence, and
//   _C2FenceFactory::CreateMultipleFdSyncFence

TEST_F(C2FenceTest, SyncFence_with_negative_fd) {
    // Create a SyncFence with a negative fd.
    C2Fence fence = _C2FenceFactory::CreateSyncFence(-1);

    // Verify that the fence is valid.
    EXPECT_TRUE(fence.valid());
    EXPECT_TRUE(fence.ready());
    EXPECT_EQ(-1, fence.fd());
    EXPECT_TRUE(fence.isHW()); // perhaps this should be false for a null fence

    native_handle_t *handle = _C2FenceFactory::CreateNativeHandle(fence);
    ASSERT_NULL(handle);
}

static int fd_kcmp(int fd1, int fd2) {
    static pid_t pid = getpid();

    return syscall(SYS_kcmp, pid, pid, KCMP_FILE, fd1, fd2);
}

TEST_F(C2FenceTest, SyncFence_with_valid_fd) {
    // Create a SyncFence with a valid fd. We cannot create an actual sync fd,
    // so we cannot test wait(), but we can verify the ABI APIs

    int fd = memfd_create("test", 0 /* flags */);

    C2Fence fence = _C2FenceFactory::CreateSyncFence(fd);
    // Verify that the fence is valid.
    EXPECT_TRUE(fence.valid());
    EXPECT_TRUE(fence.ready());
    EXPECT_TRUE(fence.isHW()); // FIXME this may be an implementation detail

    // Verify that the fd returned is a duped version of the initial fd
    int fenceFd = fence.fd();
    EXPECT_NE(-1, fenceFd);
    EXPECT_NE(fd, fenceFd);
    EXPECT_EQ(0, fd_kcmp(fd, fenceFd));

    native_handle_t *handle = _C2FenceFactory::CreateNativeHandle(fence);
    EXPECT_EQ(1, handle->numFds);
    EXPECT_EQ(1, handle->numInts);
    EXPECT_EQ(0, fd_kcmp(handle->data[0], fd));
    EXPECT_EQ(handle->data[1], 3);

    native_handle_close(handle);
    native_handle_delete(handle);
}

} // namespace android
