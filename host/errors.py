"""Machine-readable, actionable pipeline failures."""


class HostError(Exception):
    def __init__(self, code: str, message: str, *, stage: str = "inspect", details=None):
        super().__init__(message)
        self.code = code
        self.stage = stage
        self.details = details or {}

    def as_dict(self):
        return {"ok": False, "stage": self.stage, "error": {
            "code": self.code, "message": str(self), "details": self.details}}
