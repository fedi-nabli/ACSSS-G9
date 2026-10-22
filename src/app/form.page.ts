import { Component, inject } from '@angular/core';
import { FormBuilder, ReactiveFormsModule, Validators } from '@angular/forms';
import { NgIf } from '@angular/common';
import { createPasswordMatchValidator } from './validators/passwordValidators';

@Component({
  standalone: true,
  imports: [ReactiveFormsModule, NgIf],
  template: `
    <section class="page-card">
      <h2>Password match demo</h2>
      <form [formGroup]="form" class="password-form" novalidate>
        <label>
          Password
          <input type="password" formControlName="password" />
        </label>

        <label>
          Confirm password
          <input type="password" formControlName="confirmPassword" />
        </label>

        <div *ngIf="form.errors?.['passwordMismatch'] && (form.touched || form.dirty)" class="error-message">
          Passwords do not match.
        </div>

        <button type="submit" [disabled]="form.invalid">Submit</button>
      </form>
    </section>
  `
})
export class FormPage {
  private readonly fb = inject(FormBuilder);

  form = this.fb.group(
    {
      password: ['', Validators.required],
      confirmPassword: ['', Validators.required]
    },
    {
      validators: createPasswordMatchValidator()
    }
  );
}
