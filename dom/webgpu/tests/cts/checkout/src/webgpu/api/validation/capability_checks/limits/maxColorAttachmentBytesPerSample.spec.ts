import { assert } from '../../../../../common/util/util.js';
import { kTextureSampleCounts } from '../../../../capability_info.js';
import { getColorRenderAlignment, getColorRenderByteCost } from '../../../../format_info.js';
import { align } from '../../../../util/math.js';

import {
  kMaximumLimitBaseParams,
  LimitsRequest,
  LimitTestsImpl,
  makeLimitTestGroup,
} from './limit_utils.js';

const kFormatsToUseBySize = [
  'rgba32uint',
  'rgba16uint',
  'rgba8unorm',
  'rg8unorm',
  'r8unorm',
] as const;

const kInterleaveFormats = ['rgba16uint', 'rg16uint', 'rgba8unorm', 'rg8unorm', 'r8unorm'] as const;

const kFormatsUsedInTest = [...kFormatsToUseBySize, ...kInterleaveFormats] as const;
type FormatUsedInTest = (typeof kFormatsUsedInTest)[number];

function getAttachments(interleaveFormat: FormatUsedInTest, testValue: number) {
  let bytesPerSample = 0;
  const targets: GPUColorTargetState[] = [];

  const addTexture = (format: FormatUsedInTest) => {
    const newBytesPerSample =
      align(bytesPerSample, getColorRenderAlignment(format)) + getColorRenderByteCost(format);
    if (newBytesPerSample > testValue) {
      return false;
    }
    targets.push({ format, writeMask: 0 });
    bytesPerSample = newBytesPerSample;
    return true;
  };

  while (bytesPerSample < testValue) {
    addTexture(interleaveFormat);
    for (const format of kFormatsToUseBySize) {
      if (addTexture(format)) {
        break;
      }
    }
  }

  assert(bytesPerSample === testValue);
  return targets;
}

function getDescription(
  testValue: number,
  actualLimit: number,
  sampleCount: number,
  targets: GPUColorTargetState[]
) {
  return `
    // testValue  : ${testValue}
    // actualLimit: ${actualLimit}
    // sampleCount: ${sampleCount}
    // targets:
    ${(() => {
      let offset = 0;
      return targets
        .map(({ format }) => {
          const alignment = getColorRenderAlignment(format as FormatUsedInTest);
          const byteCost = getColorRenderByteCost(format as FormatUsedInTest);
          offset = align(offset, alignment);
          const s = `//   ${format.padEnd(11)} (offset: ${offset
            .toString()
            .padStart(2)}, align: ${alignment}, size: ${byteCost})`;
          offset += byteCost;
          return s;
        })
        .join('\n    ');
    })()}
  `;
}

function getPipelineDescriptor(
  device: GPUDevice,
  actualLimit: number,
  interleaveFormat: FormatUsedInTest,
  sampleCount: number,
  testValue: number
): { pipelineDescriptor: GPURenderPipelineDescriptor; code: string } | undefined {
  const targets = getAttachments(interleaveFormat, testValue);
  if (!targets) {
    return;
  }

  const typedVec = interleaveFormat.includes('uint') ? 'vec4u' : 'vec4f';

  const code = `
    ${getDescription(testValue, actualLimit, sampleCount, targets)}
    @vertex fn vs() -> @builtin(position) vec4f {
      return vec4f(0);
    }

    @fragment fn fs() -> @location(0) ${typedVec} {
      return ${typedVec}(0);
    }
  `;
  const module = device.createShaderModule({ code });
  const pipelineDescriptor: GPURenderPipelineDescriptor = {
    layout: 'auto',
    vertex: {
      module,
      entryPoint: 'vs',
    },
    fragment: {
      module,
      entryPoint: 'fs',
      targets,
    },
    // depth should not affect the test so added to make sure the implementation does not consider it
    depthStencil: {
      depthWriteEnabled: true,
      depthCompare: 'less',
      format: 'depth24plus',
    },
    multisample: {
      count: sampleCount,
    },
  };
  return { pipelineDescriptor, code };
}

function createTextures(t: LimitTestsImpl, targets: GPUColorTargetState[]) {
  return targets.map(({ format }) =>
    t.createTextureTracked({
      size: [1, 1],
      format,
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
    })
  );
}

const kExtraLimits: LimitsRequest = {
  maxColorAttachments: 'adapterLimit',
};

const limit = 'maxColorAttachmentBytesPerSample';
export const { g, description } = makeLimitTestGroup(limit);

g.test('createRenderPipeline,at_over')
  .desc(`Test using at and over ${limit} limit in createRenderPipeline(Async)`)
  .params(
    kMaximumLimitBaseParams
      .combine('async', [false, true] as const)
      .combine('sampleCount', kTextureSampleCounts)
      .combine('interleaveFormat', kInterleaveFormats)
  )
  .fn(async t => {
    const { limitTest, testValueName, async, sampleCount, interleaveFormat } = t.params;
    await t.testDeviceWithRequestedMaximumLimits(
      limitTest,
      testValueName,
      async ({ device, testValue, actualLimit, shouldError }) => {
        const result = getPipelineDescriptor(
          device,
          actualLimit,
          interleaveFormat,
          sampleCount,
          testValue
        );
        if (!result) {
          return;
        }
        const { pipelineDescriptor, code } = result;
        const numTargets = (pipelineDescriptor.fragment!.targets as GPUColorTargetState[]).length;
        if (numTargets > device.limits.maxColorAttachments) {
          return;
        }

        await t.testCreateRenderPipeline(pipelineDescriptor, async, shouldError, code);
      },
      kExtraLimits
    );
  });

g.test('beginRenderPass,at_over')
  .desc(`Test using at and over ${limit} limit in beginRenderPass`)
  .params(
    kMaximumLimitBaseParams
      .combine('sampleCount', kTextureSampleCounts)
      .combine('interleaveFormat', kInterleaveFormats)
  )
  .fn(async t => {
    const { limitTest, testValueName, sampleCount, interleaveFormat } = t.params;
    await t.testDeviceWithRequestedMaximumLimits(
      limitTest,
      testValueName,
      async ({ device, testValue, actualLimit, shouldError }) => {
        const targets = getAttachments(interleaveFormat, testValue);
        if (targets.length > device.limits.maxColorAttachments) {
          return;
        }

        const encoder = device.createCommandEncoder();
        const textures = createTextures(t, targets);

        const pass = encoder.beginRenderPass({
          colorAttachments: textures.map(texture => ({
            view: texture.createView(),
            loadOp: 'clear',
            storeOp: 'store',
          })),
        });
        pass.end();

        await t.expectValidationError(
          () => {
            encoder.finish();
          },
          shouldError,
          getDescription(testValue, actualLimit, sampleCount, targets)
        );
      },
      kExtraLimits
    );
  });

g.test('createRenderBundle,at_over')
  .desc(`Test using at and over ${limit} limit in createRenderBundle`)
  .params(
    kMaximumLimitBaseParams
      .combine('sampleCount', kTextureSampleCounts)
      .combine('interleaveFormat', kInterleaveFormats)
  )
  .fn(async t => {
    const { limitTest, testValueName, sampleCount, interleaveFormat } = t.params;
    await t.testDeviceWithRequestedMaximumLimits(
      limitTest,
      testValueName,
      async ({ device, testValue, actualLimit, shouldError }) => {
        const targets = getAttachments(interleaveFormat, testValue);
        if (targets.length > device.limits.maxColorAttachments) {
          return;
        }

        await t.expectValidationError(
          () => {
            device.createRenderBundleEncoder({
              colorFormats: targets.map(({ format }) => format),
            });
          },
          shouldError,
          getDescription(testValue, actualLimit, sampleCount, targets)
        );
      },
      kExtraLimits
    );
  });
