// kwx_ts — Phase 5 TypeScript transpilation smoke test.
//
// Proves that a `.ts` entry file with a relative `.ts` import is transpiled by
// the embedded Sucrase bundle and runs. No wxWidgets is involved here, so this
// test exits 0 without opening a window.
//
// Run from kwx_ts:
//   build/Debug/kwx_ts.exe tests/smoke.ts

import {
    areaOfSquare,
    describeShape,
    midpoint,
    Shape,
    type Point
} from "./smoke_helper.ts";

interface Named {
    name: string;
    value: number;
}

enum Color {
    Red = "red",
    Green = "green",
    Blue = "blue"
}

function summarize(item: Named): string {
    return `${item.name}=${item.value}`;
}

const origin: Point = { x: 0, y: 0 };
const corner: Point = { x: 4, y: 8 };

const center = midpoint(origin, corner);
const squareArea = areaOfSquare(6);
const shapeLabel = describeShape(Shape.Triangle);
const color: Color = Color.Green;

const report: Named = { name: "area", value: squareArea };

console.log("kwx_ts smoke ok");
console.log(summarize(report));
console.log(`shape=${shapeLabel}`);
console.log(`color=${color}`);
console.log(`midpoint=${center.x},${center.y}`);
