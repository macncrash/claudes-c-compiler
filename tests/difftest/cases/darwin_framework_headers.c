/* <Name/file.h> resolves to an SDK framework, Name.framework/Headers/file.h,
 * and an umbrella framework's headers find their sub-frameworks
 * (CoreServices.h includes <CarbonCore/CarbonCore.h> from
 * CoreServices.framework/Frameworks). ccc searched no framework directories,
 * so every macOS program including a framework header failed. Found building
 * aws-lc, whose entropy source includes <CoreServices/CoreServices.h>. Only
 * types, macros and inline functions are used here, so nothing is linked. */
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

int main(void) {
    CFRange r = CFRangeMake(3, 9);
    printf("CFRange %ld %ld\n", (long)r.location, (long)r.length);
    printf("sizes %zu %zu %zu\n", sizeof(CFIndex), sizeof(CFOptionFlags), sizeof(CFRange));
    printf("constants %d %d %ld\n", (int)kCFCompareLessThan, (int)kCFCompareGreaterThan, (long)kCFNotFound);
    printf("UInt32 %zu SInt16 %zu OSErr %zu\n", sizeof(UInt32), sizeof(SInt16), sizeof(OSErr));
    return 0;
}
