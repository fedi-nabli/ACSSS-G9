import type { QuatWxyz, QuatXyzw } from './types.ts'

/** Contract: orientation is w,x,y,z. three.js wants x,y,z,w. */
export function quatWxyzToXyzw(q: QuatWxyz): QuatXyzw {
  return [q[1], q[2], q[3], q[0]]
}
