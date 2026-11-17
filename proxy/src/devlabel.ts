// devlabel.ts -- REGISTRATION STUB (docs/workers-logs-invocation-off.md, section 8). Both functions throw, so every row
// that needs them reads red before the code exists. The code commit replaces this file.

export function normDeviceId(_raw: string): string | null {
  throw new Error("registration stub: normDeviceId is not implemented");
}

export async function deviceLabel(_id: string): Promise<string> {
  throw new Error("registration stub: deviceLabel is not implemented");
}
